#include "renderer/framebuffer.hpp"

#include <algorithm>
#include <utility>

namespace wumpo::renderer {
namespace {

constexpr std::uint8_t bitMask(int x) noexcept {
    return static_cast<std::uint8_t>(1U << (7 - (x % 8)));
}

constexpr std::size_t byteIndex(int x, int y) noexcept {
    return static_cast<std::size_t>(y * Framebuffer::kBytesPerRow + (x / 8));
}

}  // namespace

void Framebuffer::clear(bool on) noexcept {
    pixels_.fill(on ? std::uint8_t{0xFF} : std::uint8_t{0x00});
}

void Framebuffer::setPixel(int x, int y, bool on) noexcept {
    if (!inBounds(x, y)) {
        return;
    }
    const std::size_t index = byteIndex(x, y);
    const std::uint8_t mask = bitMask(x);
    if (on) {
        pixels_[index] = static_cast<std::uint8_t>(pixels_[index] | mask);
    } else {
        pixels_[index] = static_cast<std::uint8_t>(pixels_[index] & ~mask);
    }
}

bool Framebuffer::pixel(int x, int y) const noexcept {
    if (!inBounds(x, y)) {
        return false;
    }
    return (pixels_[byteIndex(x, y)] & bitMask(x)) != 0;
}

void Framebuffer::drawLine(int x0, int y0, int x1, int y1, bool on) noexcept {
    // Bresenham is not symmetric: walked from the other end it picks different
    // pixels on ties. Canonicalize the direction first so that a line is defined
    // by its endpoints and not by the order they were passed in - otherwise a
    // golden baseline would depend on which way the caller happened to draw.
    if (x0 > x1 || (x0 == x1 && y0 > y1)) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }

    // Integer Bresenham: no floating point anywhere in rendering either, so a
    // line drawn on the device is the same line drawn in a golden baseline.
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    const int step_x = x0 < x1 ? 1 : -1;
    const int step_y = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        setPixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x0 += step_x;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += step_y;
        }
    }
}

void Framebuffer::drawRect(int x, int y, int w, int h, bool on) noexcept {
    if (w <= 0 || h <= 0) {
        return;
    }
    const int right = x + w - 1;
    const int bottom = y + h - 1;
    drawLine(x, y, right, y, on);
    drawLine(x, bottom, right, bottom, on);
    drawLine(x, y, x, bottom, on);
    drawLine(right, y, right, bottom, on);
}

void Framebuffer::fillRect(int x, int y, int w, int h, bool on) noexcept {
    if (w <= 0 || h <= 0) {
        return;
    }
    // Clip once up front rather than per pixel.
    const int x_begin = std::max(x, 0);
    const int y_begin = std::max(y, 0);
    const int x_end = std::min(x + w, kWidth);
    const int y_end = std::min(y + h, kHeight);

    for (int row = y_begin; row < y_end; ++row) {
        for (int column = x_begin; column < x_end; ++column) {
            setPixel(column, row, on);
        }
    }
}

void Framebuffer::drawSprite(int x, int y, const Sprite& sprite, bool on) noexcept {
    if (!sprite.valid()) {
        return;
    }
    const int bytes_per_row = sprite.bytesPerRow();

    for (int row = 0; row < sprite.height; ++row) {
        const int target_y = y + row;
        if (target_y < 0 || target_y >= kHeight) {
            continue;
        }
        for (int column = 0; column < sprite.width; ++column) {
            const int target_x = x + column;
            if (target_x < 0 || target_x >= kWidth) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(row * bytes_per_row + (column / 8));
            const bool lit = (sprite.bits[index] & bitMask(column)) != 0;
            if (lit) {
                setPixel(target_x, target_y, on);
            }
        }
    }
}

void Framebuffer::invert() noexcept {
    for (auto& byte : pixels_) {
        byte = static_cast<std::uint8_t>(~byte);
    }
}

}  // namespace wumpo::renderer
