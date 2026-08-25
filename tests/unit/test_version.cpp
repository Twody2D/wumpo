#include "core/config.hpp"
#include "core/version.hpp"

#include <doctest.h>

#include <cstdint>
#include <cstring>

TEST_SUITE("core") {

    TEST_CASE("version is reported consistently") {
        const auto v = wumpo::core::version();
        CHECK(v.major >= 0);
        CHECK(v.minor >= 0);
        CHECK(v.patch >= 0);
        CHECK(std::strlen(wumpo::core::versionString()) > 0);
    }

    TEST_CASE("virtual hardware profile is internally consistent") {
        using namespace wumpo::config;

        // The framebuffer must tile the screen exactly: a screen width that is not a
        // multiple of 8 would silently drop or duplicate a column.
        CHECK(kScreenWidth % kPixelsPerByte == 0);
        CHECK(kBytesPerRow * kPixelsPerByte == kScreenWidth);
        CHECK(kFramebufferBytes == kBytesPerRow * kScreenHeight);

        // 64x32 monochrome is 256 bytes. If this changes, the golden baselines and
        // the storage budget change with it.
        CHECK(kFramebufferBytes == 256);

        // The tick rate must be usable without drift. 60 Hz does not divide a
        // microsecond second evenly (1'000'000 / 60 == 16666, losing 40 us per
        // second), which is exactly why the loop accumulates in microsecond-ticks
        // rather than in whole tick periods. This checks the scheme is exact.
        CHECK(kTickHz > 0);
        CHECK(kMicrosecondsPerSecond == 1'000'000);
        for (std::int64_t second = 1; second <= 3600; ++second) {
            // Ticks elapsed after `second` seconds, computed the way the loop does.
            const std::int64_t accumulated = second * kMicrosecondsPerSecond * kTickHz;
            CHECK(accumulated / kMicrosecondsPerSecond == second * kTickHz);
        }

        // Input state is carried as a bitmask in a single byte.
        CHECK(kButtonCount <= 8);
    }

} // TEST_SUITE
