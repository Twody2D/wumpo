#include "core/config.hpp"
#include "core/loop.hpp"
#include "core/replay.hpp"
#include "core/version.hpp"
#include "game/shift.hpp"
#include "input/input_state.hpp"
#include "platform/desktop/desktop_platform.hpp"
#include "renderer/framebuffer.hpp"
#include "renderer/pbm.hpp"
#include "storage/save_data.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

using wumpo::core::Replay;
using wumpo::core::TickAccumulator;
using wumpo::game::ShiftGame;
using wumpo::input::ButtonMask;
using wumpo::input::InputState;
using wumpo::platform::desktop::DesktopPlatform;
using wumpo::platform::desktop::WindowStyle;
using wumpo::renderer::Framebuffer;

struct Options {
    std::uint64_t seed = 1;
    int scale = 8;
    bool headless = false;
    bool debug_overlay = false;
    /// Stop after this many simulation ticks. Zero means run until quit; any
    /// other value makes a run finite and therefore scriptable.
    int ticks = 0;
    std::filesystem::path screenshot;
    std::filesystem::path record_path;
    std::filesystem::path replay_path;
};

void printUsage() {
    std::printf("wumpo %s - desktop prototype of a keychain console\n"
                "\n"
                "Usage: wumpo [options]\n"
                "\n"
                "  --seed N            seed the run (default 1)\n"
                "  --scale N           display scale: 1, 2, 4 or 8 (default 8)\n"
                "  --ticks N           stop after N simulation ticks\n"
                "  --screenshot FILE   write the final frame as a PBM image and exit\n"
                "  --record FILE       write every held-button mask to a replay file\n"
                "  --replay FILE       drive input from a replay file instead of the input\n"
                "                      device, stopping when it is exhausted\n"
                "  --headless          run with no window, audio or input\n"
                "  --debug             start with the debug overlay visible\n"
                "  --help              this text\n"
                "\n"
                "Keys: arrows move, Z or Enter is A, X is B.\n"
                "      F1 restart, F3 overlay, F4 screenshot, 1/2/4/8 scale, Esc quit.\n",
                wumpo::core::versionString());
}

/// Parses an integer argument, reporting the flag by name on failure so a typo
/// says what was wrong rather than silently defaulting.
template<typename T>
bool parseNumber(std::string_view text, std::string_view flag, T& out) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, out);
    if (result.ec != std::errc{} || result.ptr != end) {
        std::fprintf(stderr, "wumpo: %.*s expects a number, got '%.*s'\n",
                     static_cast<int>(flag.size()), flag.data(), static_cast<int>(text.size()),
                     text.data());
        return false;
    }
    return true;
}

bool parseOptions(int argc, char** argv, Options& options, bool& should_exit) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        const auto next = [&](std::string_view flag, std::string_view& value) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "wumpo: %.*s needs a value\n", static_cast<int>(flag.size()),
                             flag.data());
                return false;
            }
            value = argv[++i];
            return true;
        };

        std::string_view value;
        if (argument == "--help" || argument == "-h") {
            printUsage();
            should_exit = true;
            return true;
        }
        if (argument == "--headless") {
            options.headless = true;
        } else if (argument == "--debug") {
            options.debug_overlay = true;
        } else if (argument == "--seed") {
            if (!next(argument, value) || !parseNumber(value, argument, options.seed)) {
                return false;
            }
        } else if (argument == "--scale") {
            if (!next(argument, value) || !parseNumber(value, argument, options.scale)) {
                return false;
            }
        } else if (argument == "--ticks") {
            if (!next(argument, value) || !parseNumber(value, argument, options.ticks)) {
                return false;
            }
        } else if (argument == "--screenshot") {
            if (!next(argument, value)) {
                return false;
            }
            options.screenshot = std::filesystem::path(value);
        } else if (argument == "--record") {
            if (!next(argument, value)) {
                return false;
            }
            options.record_path = std::filesystem::path(value);
        } else if (argument == "--replay") {
            if (!next(argument, value)) {
                return false;
            }
            options.replay_path = std::filesystem::path(value);
        } else {
            std::fprintf(stderr, "wumpo: unknown option '%.*s' (try --help)\n",
                         static_cast<int>(argument.size()), argument.data());
            return false;
        }
    }

    if (options.scale != 1 && options.scale != 2 && options.scale != 4 && options.scale != 8) {
        std::fprintf(stderr, "wumpo: --scale must be 1, 2, 4 or 8 for pixel-perfect output\n");
        return false;
    }
    if (!options.record_path.empty() && !options.replay_path.empty()) {
        std::fprintf(stderr, "wumpo: --record and --replay cannot be combined\n");
        return false;
    }
    return true;
}

/// Reads a whole file as text. Replay files are small - a run long enough to
/// matter is still a few kilobytes of JSON - so there is no reason to stream.
bool readReplay(const std::filesystem::path& path, Replay& replay, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "could not open " + path.string();
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    return wumpo::core::fromJson(text, replay, &error);
}

bool writeReplay(const Replay& replay, const std::filesystem::path& path) {
    std::error_code code;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), code);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        std::fprintf(stderr, "wumpo: could not write %s\n", path.string().c_str());
        return false;
    }
    file << wumpo::core::toJson(replay);
    return file.good();
}

std::vector<std::string> overlayLines(const ShiftGame& game, int fps,
                                      wumpo::input::ButtonMask buttons) {
    std::string input_text = "INPUT ";
    for (const auto button : wumpo::input::kAllButtonList) {
        if (wumpo::input::isSet(buttons, button)) {
            input_text += wumpo::input::name(button);
            input_text += ' ';
        }
    }

    return {
        "FPS " + std::to_string(fps) + "  TICK " + std::to_string(game.tickCount()) + "  SEED " +
            std::to_string(game.seed()),
        std::string("STATE ") + (game.phase() == ShiftGame::Phase::Playing ? "PLAYING" : "OVER") +
            "  SCORE " + std::to_string(game.score()) + "  BEST " +
            std::to_string(game.highScore()),
        input_text,
    };
}

bool writeFrame(const Framebuffer& frame, const std::filesystem::path& path) {
    std::error_code code;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), code);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        std::fprintf(stderr, "wumpo: could not write %s\n", path.string().c_str());
        return false;
    }
    file << wumpo::renderer::toPbm(frame);
    return file.good();
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    bool should_exit = false;
    if (!parseOptions(argc, argv, options, should_exit)) {
        return 2;
    }
    if (should_exit) {
        return 0;
    }

    std::string error;

    const bool replaying = !options.replay_path.empty();
    const bool recording = !options.record_path.empty();
    Replay replay_in;
    std::size_t replay_position = 0;
    if (replaying) {
        if (!readReplay(options.replay_path, replay_in, error)) {
            std::fprintf(stderr, "wumpo: %s: %s\n", options.replay_path.string().c_str(),
                         error.c_str());
            return 1;
        }
        options.seed = replay_in.seed;
    }
    Replay replay_out;
    replay_out.seed = options.seed;

    const WindowStyle style{.scale = options.scale, .bezel = 6, .overlay_rows = 3};
    auto platform = DesktopPlatform::create("Wumpo", style, options.headless, &error);
    if (!platform) {
        std::fprintf(stderr, "wumpo: %s\n", error.c_str());
        return 1;
    }
    platform->setOverlayVisible(options.debug_overlay);

    wumpo::storage::SaveData save_data;
    const auto load_result = wumpo::storage::load(platform->storage(), save_data);
    if (load_result == wumpo::storage::LoadResult::BadMagic ||
        load_result == wumpo::storage::LoadResult::BadChecksum ||
        load_result == wumpo::storage::LoadResult::FutureVersion) {
        // Anything other than a clean load starts fresh at a high score of
        // zero: `save_data` was left untouched, so this is just its default.
        std::fprintf(stderr, "wumpo: save data unreadable, starting fresh\n");
    }

    ShiftGame game(options.seed, static_cast<int>(save_data.high_score));
    InputState input;
    Framebuffer frame;
    TickAccumulator accumulator;

    // Headless runs are driven by tick count alone: no window to close, no
    // clock to wait for. That is what makes screenshots and replay checks
    // reproducible in CI. A replay already has its own end, reached when its
    // recorded inputs run out.
    if (options.headless && options.ticks == 0 && !replaying) {
        options.ticks = 1;
    }

    std::int64_t previous_time = platform->clock().nowMicroseconds();
    std::int64_t fps_window_start = previous_time;
    int frames_in_window = 0;
    int fps = 0;
    int ticks_run = 0;
    bool running = true;

    while (running) {
        if (!platform->pump()) {
            break;
        }

        const auto commands = platform->takeCommands();
        // A hard reset has no representation in the replay format - it is a
        // developer hotkey, not a recorded input - so it is disabled while
        // recording or replaying rather than silently desyncing one from the
        // other.
        if (commands.restart && !recording && !replaying) {
            game.reset(options.seed);
            input.reset();
            accumulator.reset();
        }
        if (commands.toggle_debug) {
            options.debug_overlay = !options.debug_overlay;
            platform->setOverlayVisible(options.debug_overlay);
        }
        if (commands.requested_scale != 0) {
            platform->setScale(commands.requested_scale);
        }

        const std::int64_t now = platform->clock().nowMicroseconds();
        // Headless has no real time to wait for, so each iteration is one tick.
        const int ticks = options.headless ? 1 : accumulator.advance(now - previous_time);
        previous_time = now;

        for (int i = 0; i < ticks; ++i) {
            ButtonMask mask = 0;
            if (replaying) {
                if (replay_position >= replay_in.inputs.size()) {
                    running = false;
                    break;
                }
                mask = replay_in.inputs[replay_position++];
            } else {
                mask = platform->input().pollButtons();
            }
            input.update(mask);
            if (recording) {
                replay_out.inputs.push_back(mask);
            }

            const ShiftGame::Sound sound = game.tick(input);
            if (!sound.silent()) {
                platform->audio().tone(sound.frequency_hz, sound.duration_ms);
            }
            ++ticks_run;
            if (options.ticks > 0 && ticks_run >= options.ticks) {
                running = false;
                break;
            }
        }

        game.render(frame);

        if (options.debug_overlay) {
            const auto lines = overlayLines(game, fps, input.downMask());
            platform->setOverlayLines(lines);
        }

        platform->display().present(frame);

        if (commands.screenshot) {
            const auto path = platform->dataDirectory() /
                              ("screenshot-" + std::to_string(game.tickCount()) + ".pbm");
            if (writeFrame(frame, path)) {
                std::printf("wrote %s\n", path.string().c_str());
            }
        }

        ++frames_in_window;
        if (now - fps_window_start >= 1'000'000) {
            fps = frames_in_window;
            frames_in_window = 0;
            fps_window_start = now;
        }
    }

    if (!options.screenshot.empty()) {
        game.render(frame);
        if (!writeFrame(frame, options.screenshot)) {
            return 1;
        }
        std::printf("wrote %s\n", options.screenshot.string().c_str());
    }

    if (recording) {
        if (!writeReplay(replay_out, options.record_path)) {
            return 1;
        }
        std::printf("wrote %s\n", options.record_path.string().c_str());
    }

    // Written once on exit, not every time the score changes: flash and EEPROM
    // wear out, and this is the backend that has to live with that on hardware.
    if (game.highScore() > static_cast<int>(save_data.high_score)) {
        save_data.high_score = static_cast<std::uint32_t>(game.highScore());
        if (!wumpo::storage::store(platform->storage(), save_data)) {
            std::fprintf(stderr, "wumpo: could not write save data\n");
        }
    }

    std::printf("wumpo %s | ticks %d | score %d | best %d | seed %llu | state %016llx\n",
                wumpo::core::versionString(), game.tickCount(), game.score(), game.highScore(),
                static_cast<unsigned long long>(game.seed()),
                static_cast<unsigned long long>(game.stateHash()));
    return 0;
}
