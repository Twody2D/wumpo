#include "core/config.hpp"
#include "core/loop.hpp"
#include "core/version.hpp"
#include "game/demo_scene.hpp"
#include "input/input_state.hpp"
#include "platform/desktop/desktop_platform.hpp"
#include "renderer/framebuffer.hpp"
#include "renderer/pbm.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using wumpo::core::TickAccumulator;
using wumpo::game::DemoScene;
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
};

void printUsage() {
    std::printf("wumpo %s - desktop prototype of a keychain console\n"
                "\n"
                "Usage: wumpo [options]\n"
                "\n"
                "  --demo              run the demo scene (currently the only thing to run)\n"
                "  --seed N            seed the run (default 1)\n"
                "  --scale N           display scale: 1, 2, 4 or 8 (default 8)\n"
                "  --ticks N           stop after N simulation ticks\n"
                "  --screenshot FILE   write the final frame as a PBM image and exit\n"
                "  --headless          run with no window, audio or input\n"
                "  --debug             start with the debug overlay visible\n"
                "  --help              this text\n"
                "\n"
                "Keys: arrows move, Z or Enter is A, X is B.\n"
                "      F1 restart, F2 demo, F3 overlay, F4 screenshot, 1/2/4/8 scale, Esc quit.\n",
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
        if (argument == "--demo") {
            continue; // the demo scene is all there is to run today
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
    return true;
}

std::vector<std::string> overlayLines(const DemoScene& scene, int fps,
                                      wumpo::input::ButtonMask buttons) {
    std::string input_text = "INPUT ";
    for (const auto button : wumpo::input::kAllButtonList) {
        if (wumpo::input::isSet(buttons, button)) {
            input_text += wumpo::input::name(button);
            input_text += ' ';
        }
    }

    return {
        "FPS " + std::to_string(fps) + "  TICK " + std::to_string(scene.tickCount()) + "  SEED " +
            std::to_string(scene.seed()),
        std::string("STATE ") + (scene.phase() == DemoScene::Phase::Playing ? "PLAYING" : "OVER") +
            "  SCORE " + std::to_string(scene.score()),
        input_text,
    };
}

bool writeFrame(const Framebuffer& frame, const std::filesystem::path& path) {
    std::error_code code;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), code);
    }
    std::ofstream file(path, std::ios::trunc);
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
    const WindowStyle style{.scale = options.scale, .bezel = 6, .overlay_rows = 3};
    auto platform = DesktopPlatform::create("Wumpo", style, options.headless, &error);
    if (!platform) {
        std::fprintf(stderr, "wumpo: %s\n", error.c_str());
        return 1;
    }
    platform->setOverlayVisible(options.debug_overlay);

    DemoScene scene(options.seed);
    InputState input;
    Framebuffer frame;
    TickAccumulator accumulator;

    // Headless runs are driven by tick count alone: no window to close, no
    // clock to wait for. That is what makes screenshots and replay checks
    // reproducible in CI.
    if (options.headless && options.ticks == 0) {
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
        if (commands.restart) {
            scene.reset(options.seed);
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
            input.update(platform->input().pollButtons());
            const DemoScene::Sound sound = scene.tick(input);
            if (!sound.silent()) {
                platform->audio().tone(sound.frequency_hz, sound.duration_ms);
            }
            ++ticks_run;
            if (options.ticks > 0 && ticks_run >= options.ticks) {
                running = false;
                break;
            }
        }

        scene.render(frame);

        if (options.debug_overlay) {
            const auto lines = overlayLines(scene, fps, input.downMask());
            platform->setOverlayLines(lines);
        }

        platform->display().present(frame);

        if (commands.screenshot) {
            const auto path = platform->dataDirectory() /
                              ("screenshot-" + std::to_string(scene.tickCount()) + ".pbm");
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
        scene.render(frame);
        if (!writeFrame(frame, options.screenshot)) {
            return 1;
        }
        std::printf("wrote %s\n", options.screenshot.string().c_str());
    }

    std::printf("wumpo %s | ticks %d | score %d | seed %llu | state %016llx\n",
                wumpo::core::versionString(), scene.tickCount(), scene.score(),
                static_cast<unsigned long long>(scene.seed()),
                static_cast<unsigned long long>(scene.stateHash()));
    return 0;
}
