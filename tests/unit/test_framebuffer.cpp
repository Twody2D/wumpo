#include "renderer/framebuffer.hpp"

#include <doctest.h>

#include <array>
#include <cstdint>

using wumpo::renderer::Framebuffer;
using wumpo::renderer::Sprite;

namespace {

/// Counts lit pixels. Used to assert that clipped drawing touches nothing it
/// should not, without spelling out every coordinate.
int litCount(const Framebuffer& fb) {
    int count = 0;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            count += fb.pixel(x, y) ? 1 : 0;
        }
    }
    return count;
}

} // namespace

TEST_SUITE("renderer") {

    TEST_CASE("a fresh framebuffer is empty and exactly 256 bytes") {
        const Framebuffer fb;
        CHECK(fb.bytes().size() == 256);
        CHECK(litCount(fb) == 0);
    }

    TEST_CASE("clear fills and empties the whole screen") {
        Framebuffer fb;
        fb.clear(true);
        CHECK(litCount(fb) == Framebuffer::kWidth * Framebuffer::kHeight);
        fb.clear();
        CHECK(litCount(fb) == 0);
    }

    TEST_CASE("pixels round-trip at the boundaries of every byte") {
        Framebuffer fb;
        // Bit order matters: the leftmost pixel of a byte is the most significant
        // bit. Getting this backwards still round-trips through setPixel/pixel, so
        // check the raw byte too.
        fb.setPixel(0, 0, true);
        CHECK(fb.pixel(0, 0));
        CHECK(fb.bytes()[0] == 0x80);

        fb.clear();
        fb.setPixel(7, 0, true);
        CHECK(fb.bytes()[0] == 0x01);

        fb.clear();
        fb.setPixel(8, 0, true);
        CHECK(fb.bytes()[0] == 0x00);
        CHECK(fb.bytes()[1] == 0x80);

        fb.clear();
        fb.setPixel(63, 31, true);
        CHECK(fb.pixel(63, 31));
        CHECK(fb.bytes()[Framebuffer::kByteCount - 1] == 0x01);
    }

    TEST_CASE("out of bounds access is ignored, not clamped or wrapped") {
        Framebuffer fb;
        fb.setPixel(-1, 0, true);
        fb.setPixel(0, -1, true);
        fb.setPixel(Framebuffer::kWidth, 0, true);
        fb.setPixel(0, Framebuffer::kHeight, true);
        fb.setPixel(1000, 1000, true);
        CHECK(litCount(fb) == 0);

        // Reading out of bounds is unlit, never a crash.
        CHECK_FALSE(fb.pixel(-1, -1));
        CHECK_FALSE(fb.pixel(Framebuffer::kWidth, Framebuffer::kHeight));
    }

    TEST_CASE("horizontal and vertical lines cover exactly their endpoints") {
        Framebuffer fb;
        fb.drawLine(2, 5, 10, 5, true);
        CHECK(litCount(fb) == 9);
        CHECK(fb.pixel(2, 5));
        CHECK(fb.pixel(10, 5));
        CHECK_FALSE(fb.pixel(1, 5));
        CHECK_FALSE(fb.pixel(11, 5));

        fb.clear();
        fb.drawLine(3, 0, 3, 31, true);
        CHECK(litCount(fb) == 32);
    }

    TEST_CASE("a diagonal line is symmetric regardless of direction") {
        Framebuffer fb_forward;
        Framebuffer fb_backward;
        fb_forward.drawLine(0, 0, 20, 10, true);
        fb_backward.drawLine(20, 10, 0, 0, true);
        // Bresenham can pick a different pixel row when walked backwards; for
        // deterministic golden tests it must not.
        CHECK(fb_forward == fb_backward);
    }

    TEST_CASE("lines clip instead of wrapping to the next row") {
        Framebuffer fb;
        fb.drawLine(-10, 5, 5, 5, true);
        // Only x in [0, 5] is on screen: six pixels, all on row 5.
        CHECK(litCount(fb) == 6);
        for (int x = 0; x <= 5; ++x) {
            CHECK(fb.pixel(x, 5));
        }
        CHECK_FALSE(fb.pixel(63, 4));
    }

    TEST_CASE("rect draws an outline and fillRect draws a solid block") {
        Framebuffer outline;
        outline.drawRect(0, 0, 4, 4, true);
        CHECK(litCount(outline) == 12); // 4x4 perimeter
        CHECK(outline.pixel(0, 0));
        CHECK_FALSE(outline.pixel(1, 1));

        Framebuffer solid;
        solid.fillRect(0, 0, 4, 4, true);
        CHECK(litCount(solid) == 16);
        CHECK(solid.pixel(1, 1));
    }

    TEST_CASE("degenerate rectangles draw nothing") {
        Framebuffer fb;
        fb.drawRect(5, 5, 0, 10, true);
        fb.drawRect(5, 5, 10, 0, true);
        fb.fillRect(5, 5, -3, 4, true);
        CHECK(litCount(fb) == 0);
    }

    TEST_CASE("fillRect clips to the screen") {
        Framebuffer fb;
        fb.fillRect(-2, -2, 4, 4, true);
        CHECK(litCount(fb) == 4); // only the bottom-right quadrant is on screen
        CHECK(fb.pixel(0, 0));
        CHECK(fb.pixel(1, 1));
        CHECK_FALSE(fb.pixel(2, 0));
    }

    TEST_CASE("sprites composite over the background instead of overwriting it") {
        // A 3x2 sprite: row 0 = 101, row 1 = 010, padded to one byte per row.
        constexpr std::array<std::uint8_t, 2> kBits{0b1010'0000, 0b0100'0000};
        const Sprite sprite{kBits, 3, 2};
        REQUIRE(sprite.valid());

        Framebuffer fb;
        fb.setPixel(1, 0, true); // background pixel under an unset sprite bit
        fb.drawSprite(0, 0, sprite, true);

        CHECK(fb.pixel(0, 0));
        CHECK(fb.pixel(1, 0)); // survived: unset sprite bits do not clear
        CHECK(fb.pixel(2, 0));
        CHECK(fb.pixel(1, 1));
        CHECK_FALSE(fb.pixel(0, 1));
        CHECK(litCount(fb) == 4);
    }

    TEST_CASE("sprites clip at every edge") {
        constexpr std::array<std::uint8_t, 2> kBits{0xFF, 0xFF};
        const Sprite sprite{kBits, 8, 2};

        Framebuffer fb;
        fb.drawSprite(-4, -1, sprite, true);
        CHECK(litCount(fb) == 4); // one visible row, four visible columns

        fb.clear();
        fb.drawSprite(Framebuffer::kWidth - 2, Framebuffer::kHeight - 1, sprite, true);
        CHECK(litCount(fb) == 2);
    }

    TEST_CASE("an invalid sprite is ignored rather than read out of bounds") {
        constexpr std::array<std::uint8_t, 1> kTooSmall{0xFF};
        const Sprite sprite{kTooSmall, 8, 4}; // needs 4 bytes, has 1
        CHECK_FALSE(sprite.valid());

        Framebuffer fb;
        fb.drawSprite(0, 0, sprite, true);
        CHECK(litCount(fb) == 0);
    }

    TEST_CASE("invert flips every pixel") {
        Framebuffer fb;
        fb.setPixel(3, 3, true);
        fb.invert();
        CHECK_FALSE(fb.pixel(3, 3));
        CHECK(litCount(fb) == Framebuffer::kWidth * Framebuffer::kHeight - 1);
    }

} // TEST_SUITE
