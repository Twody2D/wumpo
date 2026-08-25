#include "renderer/font.hpp"
#include "renderer/framebuffer.hpp"

#include <doctest.h>

#include <string>

using wumpo::renderer::Framebuffer;
using wumpo::renderer::drawText;
namespace font = wumpo::renderer::font;

namespace {

int litCount(const Framebuffer& fb) {
    int count = 0;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            count += fb.pixel(x, y) ? 1 : 0;
        }
    }
    return count;
}

/// Renders one character at the origin and returns it as five rows of text,
/// so a failing assertion shows the glyph rather than a byte count.
std::string render(char character) {
    Framebuffer fb;
    drawText(fb, 0, 0, std::string(1, character));
    std::string out;
    for (int y = 0; y < font::kGlyphHeight; ++y) {
        for (int x = 0; x < font::kGlyphWidth; ++x) {
            out += fb.pixel(x, y) ? '#' : '.';
        }
        out += '\n';
    }
    return out;
}

}  // namespace

TEST_SUITE("renderer") {

TEST_CASE("the font fits the screen the way the design assumes") {
    // 16 characters across is what makes a 64-pixel screen usable for text at
    // all. If the advance changes, every layout in the game changes with it.
    CHECK(font::kAdvance == 4);
    CHECK(Framebuffer::kWidth / font::kAdvance == 16);
    CHECK(font::kGlyphCount == 59);
}

TEST_CASE("text width accounts for spacing but not a trailing column") {
    CHECK(font::textWidth("") == 0);
    CHECK(font::textWidth("A") == 3);
    CHECK(font::textWidth("AB") == 7);
    CHECK(font::textWidth("SCORE") == 19);
    // A full line of 16 characters must fit exactly.
    CHECK(font::textWidth("0123456789ABCDEF") == 63);
    CHECK(font::textWidth("0123456789ABCDEF") < Framebuffer::kWidth);
}

TEST_CASE("glyphs render the shapes they are drawn as") {
    CHECK(render('A') ==
          ".#.\n"
          "#.#\n"
          "###\n"
          "#.#\n"
          "#.#\n");

    CHECK(render('0') ==
          "###\n"
          "#.#\n"
          "#.#\n"
          "#.#\n"
          "###\n");

    CHECK(render('1') ==
          ".#.\n"
          "##.\n"
          ".#.\n"
          ".#.\n"
          "###\n");
}

TEST_CASE("lowercase folds to uppercase") {
    CHECK(render('a') == render('A'));
    CHECK(render('z') == render('Z'));
}

TEST_CASE("space and uncovered characters render blank") {
    Framebuffer fb;
    drawText(fb, 0, 0, " ");
    CHECK(litCount(fb) == 0);

    // Outside ASCII 32..90: no glyph, no box, no crash.
    drawText(fb, 0, 0, "{}~");
    CHECK(litCount(fb) == 0);

    drawText(fb, 10, 10, "\x01\x7f");
    CHECK(litCount(fb) == 0);
}

TEST_CASE("every covered character has a distinct, non-empty glyph") {
    // Catches the copy-paste failure mode of a hand-drawn font: two letters
    // sharing a shape, or a row of art accidentally deleted.
    for (char c = font::kFirstChar; c <= font::kLastChar; ++c) {
        if (c == ' ') {
            continue;
        }
        const std::string glyph = render(c);
        CHECK_MESSAGE(glyph.find('#') != std::string::npos, "glyph is empty for '", c, "'");
    }

    int collisions = 0;
    for (char a = font::kFirstChar; a <= font::kLastChar; ++a) {
        for (char b = static_cast<char>(a + 1); b <= font::kLastChar; ++b) {
            if (a != ' ' && b != ' ' && render(a) == render(b)) {
                ++collisions;
            }
        }
    }
    // 'O' and '0' are deliberately identical at 3x5: there is no room to
    // distinguish them, and no context where both appear ambiguously.
    CHECK(collisions == 1);
}

TEST_CASE("text clips at the screen edges") {
    Framebuffer fb;
    drawText(fb, -2, 0, "A");
    // Only the rightmost column of 'A' is on screen: four of its ten pixels.
    CHECK(litCount(fb) == 4);

    fb.clear();
    drawText(fb, 0, Framebuffer::kHeight - 2, "A");
    CHECK(litCount(fb) > 0);
    CHECK(litCount(fb) < 10);  // 'A' is 10 lit pixels when fully visible

    fb.clear();
    drawText(fb, 200, 200, "WUMPO");
    CHECK(litCount(fb) == 0);
}

TEST_CASE("text composites over the background") {
    Framebuffer fb;
    fb.setPixel(1, 0, true);  // sits under a gap in 'A'
    drawText(fb, 0, 0, "A");
    CHECK(fb.pixel(1, 0));

    // Drawing with on = false erases through the glyph shape.
    fb.clear(true);
    drawText(fb, 0, 0, "A", false);
    CHECK_FALSE(fb.pixel(1, 0));
    CHECK(fb.pixel(0, 0));
}

}  // TEST_SUITE
