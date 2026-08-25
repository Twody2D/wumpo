#pragma once

#include "core/config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace wumpo::renderer {

/// A non-owning 1-bit sprite: rows packed most-significant-bit first, each row
/// padded to a whole byte.
struct Sprite {
    std::span<const std::uint8_t> bits;
    int width = 0;
    int height = 0;

    [[nodiscard]] constexpr int bytesPerRow() const noexcept { return (width + 7) / 8; }
    [[nodiscard]] constexpr bool valid() const noexcept {
        return width > 0 && height > 0 &&
               bits.size() >=
                   static_cast<std::size_t>(bytesPerRow()) * static_cast<std::size_t>(height);
    }
};

/// The device screen: 64x32 pixels, one bit each, packed 8 horizontal pixels per
/// byte with the most significant bit leftmost. Exactly 256 bytes, no allocation,
/// no hidden state.
///
/// Every primitive clips silently. Drawing off-screen is normal (a moving object
/// leaves the screen), not an error, and must never corrupt neighbouring rows.
///
/// See docs/decisions/ADR-003-monochrome-rendering.md for the packing choice.
class Framebuffer {
public:
    static constexpr int kWidth = config::kScreenWidth;
    static constexpr int kHeight = config::kScreenHeight;
    static constexpr int kBytesPerRow = config::kBytesPerRow;
    static constexpr std::size_t kByteCount = config::kFramebufferBytes;

    /// Fills the whole screen. Default clears to unlit.
    void clear(bool on = false) noexcept;

    void setPixel(int x, int y, bool on) noexcept;
    [[nodiscard]] bool pixel(int x, int y) const noexcept;

    /// Integer Bresenham. Endpoints are inclusive.
    void drawLine(int x0, int y0, int x1, int y1, bool on) noexcept;

    /// Outline only; `w` and `h` are the outer dimensions.
    void drawRect(int x, int y, int w, int h, bool on) noexcept;
    void fillRect(int x, int y, int w, int h, bool on) noexcept;

    /// Draws set sprite bits as `on`; unset bits are left untouched, so sprites
    /// composite rather than overwrite the background.
    void drawSprite(int x, int y, const Sprite& sprite, bool on = true) noexcept;

    /// Flips every pixel on screen.
    void invert() noexcept;

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return pixels_; }
    [[nodiscard]] std::span<std::uint8_t> mutableBytes() noexcept { return pixels_; }

    [[nodiscard]] bool operator==(const Framebuffer& other) const noexcept {
        return pixels_ == other.pixels_;
    }

    [[nodiscard]] static constexpr bool inBounds(int x, int y) noexcept {
        return x >= 0 && x < kWidth && y >= 0 && y < kHeight;
    }

private:
    std::array<std::uint8_t, kByteCount> pixels_{};
};

} // namespace wumpo::renderer
