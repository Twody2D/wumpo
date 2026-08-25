#pragma once

#include "renderer/framebuffer.hpp"

#include <cstdint>
#include <string_view>

namespace wumpo::renderer {

/// The 3x5 pixel font: the largest readable size that still fits 16 characters
/// across a 64-pixel screen with one pixel of spacing.
///
/// Covers ASCII 32..90 (space through 'Z'). Lowercase is folded to uppercase;
/// anything else renders blank. There is no room on this screen for a second
/// case, and a missing-glyph box would spend pixels on an error nobody can act
/// on.
namespace font {

inline constexpr int kGlyphWidth = 3;
inline constexpr int kGlyphHeight = 5;
inline constexpr int kAdvance = kGlyphWidth + 1; // one pixel of letter spacing

inline constexpr char kFirstChar = ' '; // 32
inline constexpr char kLastChar = 'Z';  // 90
inline constexpr int kGlyphCount = kLastChar - kFirstChar + 1;

/// Width in pixels of `text` as drawn, excluding the trailing spacing column.
[[nodiscard]] int textWidth(std::string_view text) noexcept;

/// One column of one glyph, bit 0 = top row. Blank for anything outside the
/// covered range.
[[nodiscard]] std::uint8_t glyphColumn(char character, int column) noexcept;

} // namespace font

/// Draws `text` with its top-left corner at (x, y). Clips like every other
/// primitive; unset pixels are left alone, so text composites over background.
void drawText(Framebuffer& fb, int x, int y, std::string_view text, bool on = true) noexcept;

} // namespace wumpo::renderer
