#include "platform/desktop/desktop_platform.hpp"

#include "renderer/font.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>
#include <vector>

namespace wumpo::platform::desktop {
namespace {

/// Screen colours chosen to read as a small reflective LCD rather than a
/// monitor: a lit pixel is warm off-white, an unlit one is barely lighter than
/// the case, so the grid stays visible the way a real panel's does.
constexpr std::uint32_t kPixelOn = 0x00E8EAD8U;
constexpr std::uint32_t kPixelOff = 0x00232821U;

constexpr SDL_Color kCaseColor{.r = 26, .g = 27, .b = 24, .a = 255};
constexpr SDL_Color kBezelColor{.r = 44, .g = 46, .b = 41, .a = 255};
constexpr SDL_Color kOverlayColor{.r = 120, .g = 132, .b = 112, .a = 255};

/// Room reserved under the screen for the debug overlay, in device pixels.
/// Always reserved, even when the overlay is hidden, so toggling F3 does not
/// resize the window under the player.
constexpr int kOverlayRows = 3;
constexpr int kOverlayRowHeight = 7;
constexpr int kOverlayArea = kOverlayRows * kOverlayRowHeight;

constexpr int kAudioRate = 22'050;
constexpr int kAudioChunkMs = 40;
/// Peak amplitude. A buzzer is loud and square; this is deliberately quiet.
constexpr int kAudioAmplitude = 3'800;
/// A couple of milliseconds of ramp at each end of a tone. A hard-switched
/// square wave clicks on every note, which is authentic to a piezo and
/// unpleasant through headphones.
constexpr int kAudioRampSamples = kAudioRate * 2 / 1000;

struct QueuedTone {
    std::uint16_t frequency_hz = 0;
    int samples_remaining = 0;
};

std::filesystem::path resolveDataDirectory() {
    // The override documented in .env.example, mostly so tests and CI can keep
    // their writes inside a temporary directory.
    if (const char* override_dir = SDL_getenv("WUMPO_DATA_DIR");
        override_dir != nullptr && *override_dir != '\0') {
        return std::filesystem::path(override_dir);
    }
#ifdef _WIN32
    if (const char* local = SDL_getenv("LOCALAPPDATA"); local != nullptr) {
        return std::filesystem::path(local) / "Wumpo";
    }
#else
    if (const char* xdg = SDL_getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "wumpo";
    }
    if (const char* home = SDL_getenv("HOME"); home != nullptr) {
        return std::filesystem::path(home) / ".local" / "share" / "wumpo";
    }
#endif
    return std::filesystem::current_path() / "wumpo-data";
}

} // namespace

struct DesktopPlatform::Impl {
    // ---- host state -------------------------------------------------------
    bool headless = false;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* screen = nullptr;
    SDL_AudioStream* audio_stream = nullptr;

    WindowStyle style;
    bool quit_requested = false;
    HostCommands commands;
    std::vector<std::string> overlay_lines;
    bool overlay_visible = false;

    std::filesystem::path data_directory;
    std::filesystem::path save_path;

    /// One 32-bit pixel per device pixel, uploaded whole every frame. At 2048
    /// pixels there is nothing to gain from tracking dirty regions.
    std::array<std::uint32_t, config::kScreenWidth * config::kScreenHeight> pixels{};

    // ---- audio state ------------------------------------------------------
    std::array<QueuedTone, config::kToneQueueCapacity> tone_queue{};
    int tone_count = 0;
    QueuedTone current_tone;
    int current_tone_length = 0;
    double phase = 0.0;

    // ---- backends ---------------------------------------------------------
    class DisplayImpl final : public platform::Display {
    public:
        explicit DisplayImpl(Impl& owner) : owner_(owner) {}
        void present(const renderer::Framebuffer& frame) override { owner_.present(frame); }

    private:
        Impl& owner_;
    };

    class InputImpl final : public platform::InputSource {
    public:
        explicit InputImpl(Impl& owner) : owner_(owner) {}
        [[nodiscard]] input::ButtonMask pollButtons() override { return owner_.pollButtons(); }

    private:
        Impl& owner_;
    };

    class AudioImpl final : public platform::Audio {
    public:
        explicit AudioImpl(Impl& owner) : owner_(owner) {}
        void tone(std::uint16_t frequency_hz, std::uint16_t duration_ms) override {
            owner_.queueTone(frequency_hz, duration_ms);
        }
        void stopAll() override { owner_.stopAllTones(); }

    private:
        Impl& owner_;
    };

    class StorageImpl final : public platform::Storage {
    public:
        explicit StorageImpl(Impl& owner) : owner_(owner) {}
        [[nodiscard]] bool read(std::span<std::uint8_t> out) override { return owner_.read(out); }
        [[nodiscard]] bool write(std::span<const std::uint8_t> data) override {
            return owner_.write(data);
        }
        [[nodiscard]] std::size_t capacity() const override { return config::kStorageBytes; }

    private:
        Impl& owner_;
    };

    class ClockImpl final : public platform::Clock {
    public:
        [[nodiscard]] std::int64_t nowMicroseconds() override {
            // Nanoseconds from SDL, divided down. Monotonic by contract, which
            // the frame loop relies on.
            return static_cast<std::int64_t>(SDL_GetTicksNS() / 1000U);
        }
    };

    DisplayImpl display_backend{*this};
    InputImpl input_backend{*this};
    AudioImpl audio_backend{*this};
    StorageImpl storage_backend{*this};
    ClockImpl clock_backend;

    // ---- implementation ---------------------------------------------------
    [[nodiscard]] int screenPixelWidth() const {
        return (config::kScreenWidth + 2 * style.bezel) * style.scale;
    }
    [[nodiscard]] int screenPixelHeight() const {
        return (config::kScreenHeight + 2 * style.bezel + kOverlayArea) * style.scale;
    }

    void present(const renderer::Framebuffer& frame);
    void drawOverlay();
    void drawOverlayText(int x, int y, std::string_view text, int glyph_scale);

    [[nodiscard]] input::ButtonMask pollButtons() const;

    void queueTone(std::uint16_t frequency_hz, std::uint16_t duration_ms);
    void stopAllTones();
    void feedAudio();

    [[nodiscard]] bool read(std::span<std::uint8_t> out) const;
    [[nodiscard]] bool write(std::span<const std::uint8_t> data) const;
};

void DesktopPlatform::Impl::present(const renderer::Framebuffer& frame) {
    if (headless || renderer == nullptr) {
        return;
    }

    for (int y = 0; y < config::kScreenHeight; ++y) {
        for (int x = 0; x < config::kScreenWidth; ++x) {
            const auto index =
                static_cast<std::size_t>(y) * config::kScreenWidth + static_cast<std::size_t>(x);
            pixels[index] = frame.pixel(x, y) ? kPixelOn : kPixelOff;
        }
    }

    SDL_UpdateTexture(screen, nullptr, pixels.data(),
                      config::kScreenWidth * static_cast<int>(sizeof(std::uint32_t)));

    SDL_SetRenderDrawColor(renderer, kCaseColor.r, kCaseColor.g, kCaseColor.b, kCaseColor.a);
    SDL_RenderClear(renderer);

    const auto scale = static_cast<float>(style.scale);
    const auto bezel = static_cast<float>(style.bezel);

    // A one-pixel lip around the screen, so the panel reads as inset into a case
    // rather than printed on it.
    const SDL_FRect lip{.x = (bezel - 1) * scale,
                        .y = (bezel - 1) * scale,
                        .w = static_cast<float>(config::kScreenWidth + 2) * scale,
                        .h = static_cast<float>(config::kScreenHeight + 2) * scale};
    SDL_SetRenderDrawColor(renderer, kBezelColor.r, kBezelColor.g, kBezelColor.b, kBezelColor.a);
    SDL_RenderFillRect(renderer, &lip);

    const SDL_FRect destination{.x = bezel * scale,
                                .y = bezel * scale,
                                .w = static_cast<float>(config::kScreenWidth) * scale,
                                .h = static_cast<float>(config::kScreenHeight) * scale};
    SDL_RenderTexture(renderer, screen, nullptr, &destination);

    if (overlay_visible) {
        drawOverlay();
    }

    SDL_RenderPresent(renderer);
}

void DesktopPlatform::Impl::drawOverlay() {
    // The overlay lives under the screen, in the case, never on the 64x32 panel.
    // Debug text has no business eating pixels the game is supposed to own.
    const int glyph_scale = std::max(1, style.scale / 2);
    const int top = (style.bezel * 2 + config::kScreenHeight) * style.scale;

    SDL_SetRenderDrawColor(renderer, kOverlayColor.r, kOverlayColor.g, kOverlayColor.b,
                           kOverlayColor.a);
    const std::size_t rows = std::min<std::size_t>(overlay_lines.size(), kOverlayRows);
    for (std::size_t row = 0; row < rows; ++row) {
        const int y =
            top + static_cast<int>(row) * (renderer::font::kGlyphHeight + 2) * glyph_scale;
        drawOverlayText(style.bezel * style.scale / 2, y, overlay_lines[row], glyph_scale);
    }
}

void DesktopPlatform::Impl::drawOverlayText(int x, int y, std::string_view text, int glyph_scale) {
    // Reuses the device font rather than pulling in a text library: the overlay
    // should look like it belongs to the same object as the screen.
    int pen_x = x;
    for (const char character : text) {
        for (int column = 0; column < renderer::font::kGlyphWidth; ++column) {
            const std::uint8_t bits = renderer::font::glyphColumn(character, column);
            for (int row = 0; row < renderer::font::kGlyphHeight; ++row) {
                if ((bits & (1U << row)) == 0) {
                    continue;
                }
                const SDL_FRect pixel{.x = static_cast<float>(pen_x + column * glyph_scale),
                                      .y = static_cast<float>(y + row * glyph_scale),
                                      .w = static_cast<float>(glyph_scale),
                                      .h = static_cast<float>(glyph_scale)};
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
        pen_x += renderer::font::kAdvance * glyph_scale;
    }
}

input::ButtonMask DesktopPlatform::Impl::pollButtons() const {
    if (headless) {
        return 0;
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys == nullptr) {
        return 0;
    }

    input::ButtonMask mask = 0;
    const auto press = [&](SDL_Scancode code, input::Button button) {
        if (keys[code]) {
            mask = static_cast<input::ButtonMask>(mask | input::maskOf(button));
        }
    };

    press(SDL_SCANCODE_LEFT, input::Button::Left);
    press(SDL_SCANCODE_RIGHT, input::Button::Right);
    press(SDL_SCANCODE_UP, input::Button::Up);
    press(SDL_SCANCODE_DOWN, input::Button::Down);
    press(SDL_SCANCODE_Z, input::Button::A);
    press(SDL_SCANCODE_RETURN, input::Button::A); // convenience only; no seventh button
    press(SDL_SCANCODE_X, input::Button::B);
    return mask;
}

void DesktopPlatform::Impl::queueTone(std::uint16_t frequency_hz, std::uint16_t duration_ms) {
    if (headless || audio_stream == nullptr) {
        return;
    }
    if (tone_count >= static_cast<int>(tone_queue.size())) {
        // A full queue drops the newest tone rather than growing. The device has
        // one buzzer and no heap; a game that outruns this is asking for sound
        // it could never play on hardware either.
        return;
    }
    const int samples = std::max(1, kAudioRate * static_cast<int>(duration_ms) / 1000);
    tone_queue[static_cast<std::size_t>(tone_count)] =
        QueuedTone{.frequency_hz = frequency_hz, .samples_remaining = samples};
    ++tone_count;
}

void DesktopPlatform::Impl::stopAllTones() {
    tone_count = 0;
    current_tone = QueuedTone{};
    current_tone_length = 0;
    phase = 0.0;
    if (audio_stream != nullptr) {
        SDL_ClearAudioStream(audio_stream);
    }
}

void DesktopPlatform::Impl::feedAudio() {
    if (headless || audio_stream == nullptr) {
        return;
    }

    // Keep roughly one chunk buffered. Pushing from the frame loop instead of a
    // callback keeps the tone queue single-threaded, so there is no lock and no
    // way for audio to observe half-written game state.
    const int target_bytes = kAudioRate * kAudioChunkMs / 1000 * static_cast<int>(sizeof(Sint16));
    if (SDL_GetAudioStreamQueued(audio_stream) >= target_bytes) {
        return;
    }

    const int sample_count = kAudioRate * kAudioChunkMs / 1000;
    std::vector<Sint16> buffer(static_cast<std::size_t>(sample_count), 0);

    for (int i = 0; i < sample_count; ++i) {
        if (current_tone.samples_remaining <= 0 && tone_count > 0) {
            current_tone = tone_queue[0];
            current_tone_length = current_tone.samples_remaining;
            std::rotate(tone_queue.begin(), tone_queue.begin() + 1,
                        tone_queue.begin() + tone_count);
            --tone_count;
            phase = 0.0;
        }

        if (current_tone.samples_remaining <= 0) {
            break; // nothing queued: leave the rest of the buffer silent
        }

        if (current_tone.frequency_hz > 0) {
            const double step = static_cast<double>(current_tone.frequency_hz) / kAudioRate;
            phase += step;
            if (phase >= 1.0) {
                phase -= 1.0;
            }

            // Linear ramp at both ends of the tone to take the click off a
            // hard-switched square wave.
            const int played = current_tone_length - current_tone.samples_remaining;
            const int ramp = std::min({kAudioRampSamples, played, current_tone.samples_remaining});
            const int amplitude =
                kAudioAmplitude * std::max(0, ramp) / std::max(1, kAudioRampSamples);

            buffer[static_cast<std::size_t>(i)] =
                static_cast<Sint16>(phase < 0.5 ? amplitude : -amplitude);
        }

        --current_tone.samples_remaining;
    }

    SDL_PutAudioStreamData(audio_stream, buffer.data(),
                           static_cast<int>(buffer.size() * sizeof(Sint16)));
}

bool DesktopPlatform::Impl::read(std::span<std::uint8_t> out) const {
    std::ifstream file(save_path, std::ios::binary);
    if (!file) {
        return false; // no save yet is not an error
    }
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return file.gcount() == static_cast<std::streamsize>(out.size());
}

bool DesktopPlatform::Impl::write(std::span<const std::uint8_t> data) const {
    if (data.size() > config::kStorageBytes) {
        return false;
    }
    std::error_code code;
    std::filesystem::create_directories(data_directory, code);
    if (code) {
        return false;
    }
    std::ofstream file(save_path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    return file.good();
}

DesktopPlatform::DesktopPlatform() : impl_(std::make_unique<Impl>()) {
}

DesktopPlatform::~DesktopPlatform() {
    if (impl_->audio_stream != nullptr) {
        SDL_DestroyAudioStream(impl_->audio_stream);
    }
    if (impl_->screen != nullptr) {
        SDL_DestroyTexture(impl_->screen);
    }
    if (impl_->renderer != nullptr) {
        SDL_DestroyRenderer(impl_->renderer);
    }
    if (impl_->window != nullptr) {
        SDL_DestroyWindow(impl_->window);
    }
    if (!impl_->headless) {
        SDL_Quit();
    }
}

std::unique_ptr<DesktopPlatform> DesktopPlatform::create(std::string_view title, WindowStyle style,
                                                         bool headless, std::string* error) {
    auto platform = std::unique_ptr<DesktopPlatform>(new DesktopPlatform());
    Impl& impl = *platform->impl_;
    impl.headless = headless;
    impl.style = style;
    impl.data_directory = resolveDataDirectory();
    impl.save_path = impl.data_directory / "wumpo.sav";

    if (headless) {
        return platform; // no window, no audio, no events
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        if (error != nullptr) {
            *error = std::string("SDL_Init failed: ") + SDL_GetError();
        }
        return nullptr;
    }

    impl.window = SDL_CreateWindow(std::string(title).c_str(), impl.screenPixelWidth(),
                                   impl.screenPixelHeight(), 0);
    if (impl.window == nullptr) {
        if (error != nullptr) {
            *error = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        }
        return nullptr;
    }

    impl.renderer = SDL_CreateRenderer(impl.window, nullptr);
    if (impl.renderer == nullptr) {
        if (error != nullptr) {
            *error = std::string("SDL_CreateRenderer failed: ") + SDL_GetError();
        }
        return nullptr;
    }

    impl.screen =
        SDL_CreateTexture(impl.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                          config::kScreenWidth, config::kScreenHeight);
    if (impl.screen == nullptr) {
        if (error != nullptr) {
            *error = std::string("SDL_CreateTexture failed: ") + SDL_GetError();
        }
        return nullptr;
    }
    // Nearest neighbour, always. A blurred pixel is a lie about what the device
    // can display.
    SDL_SetTextureScaleMode(impl.screen, SDL_SCALEMODE_NEAREST);

    const SDL_AudioSpec spec{.format = SDL_AUDIO_S16, .channels = 1, .freq = kAudioRate};
    impl.audio_stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (impl.audio_stream != nullptr) {
        SDL_ResumeAudioStreamDevice(impl.audio_stream);
    }
    // A machine with no sound device is not a reason to refuse to run.

    return platform;
}

Display& DesktopPlatform::display() {
    return impl_->display_backend;
}
InputSource& DesktopPlatform::input() {
    return impl_->input_backend;
}
Audio& DesktopPlatform::audio() {
    return impl_->audio_backend;
}
Storage& DesktopPlatform::storage() {
    return impl_->storage_backend;
}
Clock& DesktopPlatform::clock() {
    return impl_->clock_backend;
}

bool DesktopPlatform::pump() {
    if (impl_->headless) {
        return true;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            impl_->quit_requested = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.repeat) {
                break;
            }
            switch (event.key.scancode) {
            case SDL_SCANCODE_ESCAPE:
                impl_->quit_requested = true;
                break;
            case SDL_SCANCODE_F1:
                impl_->commands.restart = true;
                break;
            case SDL_SCANCODE_F2:
                impl_->commands.toggle_demo = true;
                break;
            case SDL_SCANCODE_F3:
                impl_->commands.toggle_debug = true;
                break;
            case SDL_SCANCODE_F4:
                impl_->commands.screenshot = true;
                break;
            case SDL_SCANCODE_1:
                impl_->commands.requested_scale = 1;
                break;
            case SDL_SCANCODE_2:
                impl_->commands.requested_scale = 2;
                break;
            case SDL_SCANCODE_4:
                impl_->commands.requested_scale = 4;
                break;
            case SDL_SCANCODE_8:
                impl_->commands.requested_scale = 8;
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    impl_->feedAudio();
    return !impl_->quit_requested;
}

HostCommands DesktopPlatform::takeCommands() noexcept {
    const HostCommands taken = impl_->commands;
    impl_->commands = HostCommands{};
    return taken;
}

void DesktopPlatform::setOverlayLines(std::span<const std::string> lines) {
    impl_->overlay_lines.assign(lines.begin(), lines.end());
}

void DesktopPlatform::setOverlayVisible(bool visible) noexcept {
    impl_->overlay_visible = visible;
}

void DesktopPlatform::setScale(int scale) noexcept {
    if (scale <= 0 || scale == impl_->style.scale) {
        return;
    }
    impl_->style.scale = scale;
    if (impl_->window != nullptr) {
        SDL_SetWindowSize(impl_->window, impl_->screenPixelWidth(), impl_->screenPixelHeight());
    }
}

int DesktopPlatform::scale() const noexcept {
    return impl_->style.scale;
}

const std::filesystem::path& DesktopPlatform::dataDirectory() const noexcept {
    return impl_->data_directory;
}

} // namespace wumpo::platform::desktop
