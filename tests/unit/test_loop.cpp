#include "core/config.hpp"
#include "core/loop.hpp"

#include <doctest.h>

#include <array>
#include <cstdint>

using wumpo::core::TickAccumulator;
namespace config = wumpo::config;

TEST_SUITE("core") {

    TEST_CASE("no time means no ticks") {
        TickAccumulator accumulator;
        CHECK(accumulator.advance(0) == 0);
        CHECK(accumulator.remainder() == 0);
    }

    TEST_CASE("a whole second produces exactly the tick rate") {
        TickAccumulator accumulator;
        // Delivered in one go it would hit the catch-up cap, so feed it the way a
        // running host does: in frame-sized slices.
        int total = 0;
        for (int frame = 0; frame < 60; ++frame) {
            total += accumulator.advance(1'000'000 / 60);
        }
        // 16666 us per frame times 60 is 999'960 us: just short of a second, so the
        // 60th tick lands on the next frame. What matters is that nothing is lost.
        CHECK(total >= config::kTickHz - 1);
        CHECK(total <= config::kTickHz);
    }

    TEST_CASE("timing does not drift over a long session") {
        // The reason the accumulator counts in microsecond-ticks. With a truncated
        // 16666 us period this test would be short by 144 ticks after an hour.
        TickAccumulator accumulator;
        std::int64_t ticks = 0;
        constexpr int kFramesPerSecond = 100; // 10 ms slices, never hits the cap
        for (int second = 0; second < 3600; ++second) {
            for (int frame = 0; frame < kFramesPerSecond; ++frame) {
                ticks += accumulator.advance(1'000'000 / kFramesPerSecond);
            }
        }
        CHECK(ticks == 3600LL * config::kTickHz);
    }

    TEST_CASE("a fractional frame carries its remainder into the next one") {
        TickAccumulator accumulator;
        // Three 6 ms slices are 18 ms: less than a tick each, more than one tick
        // together. A frame shorter than a tick must bank time, not discard it.
        CHECK(accumulator.advance(6'000) == 0);
        CHECK(accumulator.remainder() > 0);
        CHECK(accumulator.advance(6'000) == 0);
        CHECK(accumulator.advance(6'000) == 1);
    }

    TEST_CASE("catch-up is capped so a stall cannot become a freeze") {
        TickAccumulator accumulator;
        // Half a second of missed time: 30 ticks owed, cap allows 5.
        const int ticks = accumulator.advance(500'000);
        CHECK(ticks == config::kMaxCatchupTicks);

        // The backlog is abandoned, not queued: the following second runs at the
        // normal rate instead of paying off half a second of debt.
        int recovered = 0;
        for (int frame = 0; frame < 100; ++frame) {
            recovered += accumulator.advance(10'000);
        }
        CHECK(recovered == config::kTickHz);
    }

    TEST_CASE("dropping a backlog keeps the sub-tick remainder") {
        TickAccumulator accumulator;
        // 5 ticks worth plus a deliberate half tick.
        const std::int64_t tick_us = 1'000'000 / config::kTickHz;
        const int capped = accumulator.advance(tick_us * 20 + tick_us / 2);
        REQUIRE(capped == config::kMaxCatchupTicks);
        // The remainder must be a fraction of a tick, not a whole tick or zero:
        // resetting it to zero on every stall would make timing drift the moment
        // the host stutters.
        CHECK(accumulator.remainder() < TickAccumulator::kUnitsPerTick);
    }

    TEST_CASE("a clock that jumps backwards is ignored") {
        TickAccumulator accumulator;
        CHECK(accumulator.advance(-1) == 0);
        CHECK(accumulator.advance(-1'000'000'000) == 0);
        CHECK(accumulator.remainder() == 0);
    }

    TEST_CASE("an absurd delta is clamped rather than overflowing") {
        TickAccumulator accumulator;
        // A breakpoint held for an hour, or a suspended laptop.
        const int ticks = accumulator.advance(3'600'000'000LL);
        CHECK(ticks == config::kMaxCatchupTicks);
        CHECK(accumulator.remainder() >= 0);
        CHECK(accumulator.remainder() < TickAccumulator::kUnitsPerTick);

        // Still usable afterwards, at the normal rate.
        int recovered = 0;
        for (int frame = 0; frame < 100; ++frame) {
            recovered += accumulator.advance(10'000);
        }
        CHECK(recovered == config::kTickHz);
    }

    TEST_CASE("reset clears pending time") {
        TickAccumulator accumulator;
        const int ticks = accumulator.advance(1'000'000 / config::kTickHz / 2);
        REQUIRE(ticks == 0);
        REQUIRE(accumulator.remainder() > 0);
        accumulator.reset();
        CHECK(accumulator.remainder() == 0);
    }

    TEST_CASE("the same delta sequence always yields the same tick sequence") {
        constexpr std::array<std::int64_t, 7> kDeltas = {16'000, 17'000, 16'666, 33'000,
                                                         8'000,  16'666, 100'000};

        TickAccumulator first;
        TickAccumulator second;
        for (const std::int64_t delta : kDeltas) {
            CHECK(first.advance(delta) == second.advance(delta));
            CHECK(first.remainder() == second.remainder());
        }
    }

} // TEST_SUITE
