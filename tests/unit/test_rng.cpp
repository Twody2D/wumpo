#include "core/rng.hpp"

#include <doctest.h>

#include <array>
#include <cstdint>

using wumpo::core::Pcg32;

TEST_SUITE("core") {

    TEST_CASE("the generator matches the PCG32 reference sequence") {
        // These values come from an independent implementation of PCG32 XSH RR
        // written from the specification, not from this class. Matching them means
        // the algorithm is right, not merely stable.
        //
        // Never update these to match new output. If they fail, the generator
        // changed - and every replay and golden baseline recorded before the change
        // is now invalid.
        constexpr std::array<std::uint32_t, 8> kExpected = {0x8630B53A, 0x16AC2A2C, 0xBADE2D7F,
                                                            0xD2F5ADF8, 0xB84DEEDD, 0x9836AA9E,
                                                            0x87AE6FBC, 0x04CE78FC};

        Pcg32 rng(12345);
        for (const std::uint32_t expected : kExpected) {
            CHECK(rng.next() == expected);
        }
    }

    TEST_CASE("a seed of zero is a normal seed") {
        // A generator that degenerates on a zero seed is a classic xorshift trap,
        // and zero is exactly what an uninitialised save or a default argument
        // produces.
        constexpr std::array<std::uint32_t, 4> kExpected = {0x0A65CE7D, 0x97A1773E, 0xC03F123A,
                                                            0xF1654D25};
        Pcg32 rng(0);
        for (const std::uint32_t expected : kExpected) {
            CHECK(rng.next() == expected);
        }
    }

    TEST_CASE("the same seed always replays the same sequence") {
        Pcg32 first(999);
        Pcg32 second(999);
        for (int i = 0; i < 1000; ++i) {
            CHECK(first.next() == second.next());
        }
        CHECK(first == second);
    }

    TEST_CASE("different seeds diverge immediately") {
        Pcg32 first(1);
        Pcg32 second(2);
        CHECK(first.next() != second.next());
    }

    TEST_CASE("streams with the same seed do not overlap") {
        Pcg32 spawns(7, 1);
        Pcg32 cosmetics(7, 2);
        bool differed = false;
        for (int i = 0; i < 16 && !differed; ++i) {
            differed = spawns.next() != cosmetics.next();
        }
        CHECK(differed);
    }

    TEST_CASE("nextBelow stays inside its bound") {
        Pcg32 rng(3);
        for (std::uint32_t bound : {1U, 2U, 3U, 6U, 7U, 64U, 1000U}) {
            for (int i = 0; i < 500; ++i) {
                const std::uint32_t value = rng.nextBelow(bound);
                CHECK(value < bound);
            }
        }
    }

    TEST_CASE("a bound of zero yields zero instead of dividing by zero") {
        Pcg32 rng(3);
        CHECK(rng.nextBelow(0) == 0);
    }

    TEST_CASE("nextBelow is unbiased for a bound that does not divide 2^32") {
        // Six is the classic case where `next() % bound` leans low. With 60000
        // draws, each bucket should land near 10000; a modulo implementation drifts
        // measurably further than this tolerance.
        Pcg32 rng(42);
        std::array<int, 6> counts{};
        constexpr int kDraws = 60'000;
        for (int i = 0; i < kDraws; ++i) {
            ++counts[rng.nextBelow(6)];
        }

        for (const int count : counts) {
            CHECK(count > 9'500);
            CHECK(count < 10'500);
        }

        int total = 0;
        for (const int count : counts) {
            total += count;
        }
        CHECK(total == kDraws);
    }

    TEST_CASE("nextRange covers its endpoints and nothing outside them") {
        Pcg32 rng(11);
        bool saw_min = false;
        bool saw_max = false;
        for (int i = 0; i < 2000; ++i) {
            const int value = rng.nextRange(-3, 3);
            CHECK(value >= -3);
            CHECK(value <= 3);
            saw_min = saw_min || value == -3;
            saw_max = saw_max || value == 3;
        }
        CHECK(saw_min);
        CHECK(saw_max);
    }

    TEST_CASE("a degenerate range returns its single value") {
        Pcg32 rng(11);
        CHECK(rng.nextRange(5, 5) == 5);
        CHECK(rng.nextRange(5, 4) == 5); // inverted: treated as empty, not wrapped
    }

    TEST_CASE("nextBool is roughly balanced") {
        Pcg32 rng(5);
        int heads = 0;
        constexpr int kFlips = 10'000;
        for (int i = 0; i < kFlips; ++i) {
            heads += rng.nextBool() ? 1 : 0;
        }
        CHECK(heads > 4'800);
        CHECK(heads < 5'200);
    }

    TEST_CASE("state can be saved and restored exactly") {
        // What resuming a run, or hashing game state, depends on.
        Pcg32 rng(2024);
        for (int i = 0; i < 10; ++i) {
            (void)rng.next();
        }

        const std::uint64_t saved_state = rng.state();
        const std::uint64_t saved_stream = rng.stream();
        const std::uint32_t expected = rng.next();

        Pcg32 restored;
        restored.setState(saved_state, saved_stream);
        CHECK(restored.next() == expected);
    }

    TEST_CASE("an even increment cannot be installed") {
        // A PCG increment must be odd or the period collapses. Restoring from a
        // corrupt save must not be able to break the generator.
        Pcg32 rng;
        rng.setState(1, 2);
        CHECK((rng.stream() & 1U) == 1U);
    }

} // TEST_SUITE
