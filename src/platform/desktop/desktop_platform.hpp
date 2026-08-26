#pragma once

#include "core/config.hpp"
#include "input/button.hpp"
#include "platform/platform.hpp"
#include "renderer/framebuffer.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

/// The SDL3 desktop backend: the only place in the project that sees an SDL
/// header, and the only place that knows what an operating system is.
///
/// Its job is to make the desktop pretend to be the device - pixel-perfect
/// integer scaling, one square-wave channel, a fixed block of save bytes - and
/// to add the few things a host needs that hardware does not: a window that can
/// be closed, and developer hotkeys.
///
/// See docs/decisions/ADR-006-sdl3-over-sdl2.md.
namespace wumpo::platform::desktop {

/// What the developer asked the emulator to do this frame, through a hotkey.
/// These are host conveniences, not device features: hardware has six buttons
/// and no function keys.
struct HostCommands {
    bool restart = false;      // F1
    bool toggle_debug = false; // F3
    bool screenshot = false;   // F4
    int requested_scale = 0;   // 1, 2, 4, 8 - zero means unchanged
};

/// How the emulator window looks. Deliberately spare: a dark case, a screen,
/// and nothing else. No toolbars, no panels, nothing that would make this feel
/// like a program rather than an object.
struct WindowStyle {
    int scale = 8;
    int bezel = 12;       // case border around the screen, in screen pixels
    int overlay_rows = 0; // extra room under the screen for the debug overlay
};

class DesktopPlatform final : public Platform {
public:
    /// Creates the window, audio device and save file location.
    ///
    /// `headless` skips every SDL subsystem: no window, no audio, no events.
    /// That is what `--headless`, screenshots and replay verification use, and
    /// it works on a machine with no display at all.
    static std::unique_ptr<DesktopPlatform> create(std::string_view title, WindowStyle style,
                                                   bool headless, std::string* error);

    DesktopPlatform(const DesktopPlatform&) = delete;
    DesktopPlatform(DesktopPlatform&&) = delete;
    DesktopPlatform& operator=(const DesktopPlatform&) = delete;
    DesktopPlatform& operator=(DesktopPlatform&&) = delete;
    ~DesktopPlatform() override;

    [[nodiscard]] Display& display() override;
    [[nodiscard]] InputSource& input() override;
    [[nodiscard]] Audio& audio() override;
    [[nodiscard]] Storage& storage() override;
    [[nodiscard]] Clock& clock() override;
    [[nodiscard]] bool pump() override;

    /// Hotkeys pressed since the last call. Reading them clears them.
    [[nodiscard]] HostCommands takeCommands() noexcept;

    /// Text drawn beneath the screen when the debug overlay is on. Set by the
    /// emulator each frame; the backend only knows how to draw it.
    void setOverlayLines(std::span<const std::string> lines);
    void setOverlayVisible(bool visible) noexcept;

    void setScale(int scale) noexcept;
    [[nodiscard]] int scale() const noexcept;

    /// Where saves, screenshots and recordings go.
    [[nodiscard]] const std::filesystem::path& dataDirectory() const noexcept;

private:
    DesktopPlatform();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wumpo::platform::desktop
