#pragma once

#include <cstdint>

namespace wumpo::core {

/// PCG32 (XSH RR 64/32), written out rather than taken from <random>.
///
/// The standard library is not an option here: `std::mt19937` is specified, but
/// the distributions that make it usable - `uniform_int_distribution` and
/// friends - are not. The same seed and the same calls produce different numbers
/// on libstdc++, libc++ and MSVC, which would mean a replay recorded on one
/// machine could not be verified on another, and a golden test could not pin a
/// spawn pattern.
///
/// PCG32 is about fifteen lines, has no state beyond two 64-bit words, needs no
/// allocation and no floating point, and passes statistical tests that a
/// hand-rolled xorshift does not. All of that matters on a microcontroller too.
///
/// See docs/decisions/ADR-002-fixed-timestep.md for the wider determinism rules.
class Pcg32 {
public:
    /// Multiplier from the reference PCG implementation.
    static constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;
    static constexpr std::uint64_t kDefaultSeed = 0x853C49E6748FEA9BULL;
    static constexpr std::uint64_t kDefaultStream = 0xDA3E39CB94B95BDBULL;

    constexpr Pcg32() noexcept : Pcg32(kDefaultSeed) {}

    /// `stream` selects one of 2^63 independent sequences. Two generators with
    /// the same seed but different streams never overlap, which is how the game
    /// can have separate streams for, say, spawns and cosmetics without one
    /// consuming the other's numbers.
    constexpr explicit Pcg32(std::uint64_t seed, std::uint64_t stream = kDefaultStream) noexcept
        : increment_((stream << 1U) | 1U) {
        // The reference seeding routine: step, add the seed, step again.
        state_ = 0;
        step();
        state_ += seed;
        step();
    }

    /// Uniform over the full 32-bit range.
    [[nodiscard]] constexpr std::uint32_t next() noexcept {
        const std::uint64_t previous = state_;
        step();
        const auto xorshifted = static_cast<std::uint32_t>(((previous >> 18U) ^ previous) >> 27U);
        const auto rotation = static_cast<std::uint32_t>(previous >> 59U);
        return (xorshifted >> rotation) | (xorshifted << ((32U - rotation) & 31U));
    }

    /// Uniform over [0, bound). Returns 0 for a bound of 0.
    ///
    /// Uses Lemire's method with rejection, not `next() % bound`: modulo is
    /// biased toward small values unless the bound divides 2^32, and at the
    /// scale of a game that shows up as a spawn pattern that leans one way.
    [[nodiscard]] constexpr std::uint32_t nextBelow(std::uint32_t bound) noexcept {
        if (bound == 0) {
            return 0;
        }
        std::uint64_t product = static_cast<std::uint64_t>(next()) * bound;
        auto low = static_cast<std::uint32_t>(product);
        if (low < bound) {
            const std::uint32_t threshold = (~bound + 1U) % bound;  // (2^32 - bound) % bound
            while (low < threshold) {
                product = static_cast<std::uint64_t>(next()) * bound;
                low = static_cast<std::uint32_t>(product);
            }
        }
        return static_cast<std::uint32_t>(product >> 32U);
    }

    /// Uniform over [min, max], inclusive. Returns `min` if the range is empty.
    [[nodiscard]] constexpr int nextRange(int min, int max) noexcept {
        if (max <= min) {
            return min;
        }
        const auto span = static_cast<std::uint32_t>(max - min) + 1U;
        return min + static_cast<int>(nextBelow(span));
    }

    /// One bit, taken from the top where the generator is strongest.
    [[nodiscard]] constexpr bool nextBool() noexcept { return (next() >> 31U) != 0; }

    /// Exposed so game state can be hashed and saved: restoring a run means
    /// restoring the generator, not just the score.
    [[nodiscard]] constexpr std::uint64_t state() const noexcept { return state_; }
    [[nodiscard]] constexpr std::uint64_t stream() const noexcept { return increment_; }

    constexpr void setState(std::uint64_t state, std::uint64_t increment) noexcept {
        state_ = state;
        increment_ = increment | 1U;  // an even increment would collapse the period
    }

    [[nodiscard]] constexpr bool operator==(const Pcg32&) const noexcept = default;

private:
    constexpr void step() noexcept { state_ = state_ * kMultiplier + increment_; }

    std::uint64_t state_ = 0;
    std::uint64_t increment_ = kDefaultStream;
};

}  // namespace wumpo::core
