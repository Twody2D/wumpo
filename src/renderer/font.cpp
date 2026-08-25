#include "renderer/font.hpp"

#include <array>
#include <cstddef>

namespace wumpo::renderer {
namespace font {
namespace {

/// The font, drawn rather than encoded: '#' is a lit pixel, anything else unlit.
/// Five rows per glyph, in ASCII order starting at space.
///
/// Written this way on purpose. A 3x5 glyph is small enough that a hex table
/// would be unreadable and unfixable, and tuning letterforms by eye is exactly
/// the kind of change this project will make often.
constexpr std::string_view kGlyphArt =
    // clang-format off
    /* space */ "..." "..." "..." "..." "..."
    /* !     */ ".#." ".#." ".#." "..." ".#."
    /* "     */ "#.#" "#.#" "..." "..." "..."
    /* #     */ "#.#" "###" "#.#" "###" "#.#"
    /* $     */ ".##" "##." ".#." ".##" "##."
    /* %     */ "#.#" "..#" ".#." "#.." "#.#"
    /* &     */ "##." "##." "###" "#.#" "###"
    /* apos  */ ".#." ".#." "..." "..." "..."
    /* (     */ "..#" ".#." ".#." ".#." "..#"
    /* )     */ "#.." ".#." ".#." ".#." "#.."
    /* *     */ "#.#" ".#." "#.#" "..." "..."
    /* +     */ "..." ".#." "###" ".#." "..."
    /* ,     */ "..." "..." "..." ".#." "#.."
    /* -     */ "..." "..." "###" "..." "..."
    /* .     */ "..." "..." "..." "..." ".#."
    /* /     */ "..#" "..#" ".#." "#.." "#.."
    /* 0     */ "###" "#.#" "#.#" "#.#" "###"
    /* 1     */ ".#." "##." ".#." ".#." "###"
    /* 2     */ "##." "..#" ".#." "#.." "###"
    /* 3     */ "##." "..#" ".#." "..#" "##."
    /* 4     */ "#.#" "#.#" "###" "..#" "..#"
    /* 5     */ "###" "#.." "##." "..#" "##."
    /* 6     */ ".##" "#.." "###" "#.#" "###"
    /* 7     */ "###" "..#" ".#." ".#." ".#."
    /* 8     */ "###" "#.#" "###" "#.#" "###"
    /* 9     */ "###" "#.#" "###" "..#" "##."
    /* :     */ "..." ".#." "..." ".#." "..."
    /* ;     */ "..." ".#." "..." ".#." "#.."
    /* <     */ "..#" ".#." "#.." ".#." "..#"
    /* =     */ "..." "###" "..." "###" "..."
    /* >     */ "#.." ".#." "..#" ".#." "#.."
    /* ?     */ "##." "..#" ".#." "..." ".#."
    /* @     */ "###" "#.#" "###" "#.." ".##"
    /* A     */ ".#." "#.#" "###" "#.#" "#.#"
    /* B     */ "##." "#.#" "##." "#.#" "##."
    /* C     */ ".##" "#.." "#.." "#.." ".##"
    /* D     */ "##." "#.#" "#.#" "#.#" "##."
    /* E     */ "###" "#.." "##." "#.." "###"
    /* F     */ "###" "#.." "##." "#.." "#.."
    /* G     */ ".##" "#.." "#.#" "#.#" ".##"
    /* H     */ "#.#" "#.#" "###" "#.#" "#.#"
    /* I     */ "###" ".#." ".#." ".#." "###"
    /* J     */ "..#" "..#" "..#" "#.#" ".#."
    /* K     */ "#.#" "#.#" "##." "#.#" "#.#"
    /* L     */ "#.." "#.." "#.." "#.." "###"
    /* M     */ "#.#" "###" "###" "#.#" "#.#"
    /* N     */ "#.#" "##." "###" ".##" "#.#"
    /* O     */ "###" "#.#" "#.#" "#.#" "###"
    /* P     */ "##." "#.#" "##." "#.." "#.."
    /* Q     */ "###" "#.#" "#.#" "###" "..#"
    /* R     */ "##." "#.#" "##." "#.#" "#.#"
    /* S     */ ".##" "#.." ".#." "..#" "##."
    /* T     */ "###" ".#." ".#." ".#." ".#."
    /* U     */ "#.#" "#.#" "#.#" "#.#" "###"
    /* V     */ "#.#" "#.#" "#.#" "#.#" ".#."
    /* W     */ "#.#" "#.#" "###" "###" "#.#"
    /* X     */ "#.#" "#.#" ".#." "#.#" "#.#"
    /* Y     */ "#.#" "#.#" ".#." ".#." ".#."
    /* Z     */ "###" "..#" ".#." "#.." "###";
// clang-format on

static_assert(kGlyphArt.size() ==
                  static_cast<std::size_t>(kGlyphCount * kGlyphWidth * kGlyphHeight),
              "glyph art must cover exactly ASCII 32..90");

using GlyphTable = std::array<std::uint8_t, static_cast<std::size_t>(kGlyphCount * kGlyphWidth)>;

/// Packs the art into columns at compile time: one byte per column, bit 0 is the
/// top row. Column-major costs three bytes per glyph instead of five, and the
/// device will have kilobytes of flash, not megabytes.
constexpr GlyphTable buildGlyphs() noexcept {
    GlyphTable table{};
    for (int glyph = 0; glyph < kGlyphCount; ++glyph) {
        for (int row = 0; row < kGlyphHeight; ++row) {
            for (int column = 0; column < kGlyphWidth; ++column) {
                const auto art_index =
                    static_cast<std::size_t>((glyph * kGlyphHeight + row) * kGlyphWidth + column);
                if (kGlyphArt[art_index] == '#') {
                    const auto out_index = static_cast<std::size_t>(glyph * kGlyphWidth + column);
                    table[out_index] = static_cast<std::uint8_t>(table[out_index] | (1U << row));
                }
            }
        }
    }
    return table;
}

constexpr GlyphTable kGlyphs = buildGlyphs();

constexpr char toUpper(char character) noexcept {
    return (character >= 'a' && character <= 'z') ? static_cast<char>(character - ('a' - 'A'))
                                                  : character;
}

}  // namespace

std::uint8_t glyphColumn(char character, int column) noexcept {
    if (column < 0 || column >= kGlyphWidth) {
        return 0;
    }
    const char upper = toUpper(character);
    if (upper < kFirstChar || upper > kLastChar) {
        return 0;
    }
    const auto index = static_cast<std::size_t>((upper - kFirstChar) * kGlyphWidth + column);
    return kGlyphs[index];
}

int textWidth(std::string_view text) noexcept {
    if (text.empty()) {
        return 0;
    }
    return static_cast<int>(text.size()) * kAdvance - 1;
}

}  // namespace font

void drawText(Framebuffer& fb, int x, int y, std::string_view text, bool on) noexcept {
    int pen_x = x;
    for (const char character : text) {
        for (int column = 0; column < font::kGlyphWidth; ++column) {
            const std::uint8_t bits = font::glyphColumn(character, column);
            for (int row = 0; row < font::kGlyphHeight; ++row) {
                if ((bits & (1U << row)) != 0) {
                    fb.setPixel(pen_x + column, y + row, on);
                }
            }
        }
        pen_x += font::kAdvance;
    }
}

}  // namespace wumpo::renderer
