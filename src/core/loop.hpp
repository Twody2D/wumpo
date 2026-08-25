#pragma once

#include "core/config.hpp"

#include <algorithm>
#include <cstdint>

namespace wumpo::core {

/// Turns elapsed real time into whole simulation ticks.
///
/// The accumulator counts in microsecond-ticks (microseconds multiplied by the
/// tick rate) and compares against one second's worth. That is exact for any
/// tick rate, unlike subtracting a truncated tick period: 1'000'000 / 60 is
/// 16666, which would lose 40 microseconds every second and drift visibly over
/// a long session. See docs/decisions/ADR-002-fixed-timestep.md.
///
/// The accumulator holds only real time. It never sees game state, so a stalled
/// or fast host changes how often ticks happen, never what a tick does.
class TickAccumulator {
public:
    /// Microsecond-ticks that make up one whole tick.
    static constexpr std::int64_t kUnitsPerTick = config::kMicrosecondsPerSecond;

    /// Ignores any single delta longer than this. A debugger breakpoint, a
    /// laptop lid closing or a clock adjustment can produce an arbitrarily large
    /// elapsed time; without a clamp it would overflow the accumulator, and with
    /// only the catch-up cap it would silently discard hours of pending time
    /// every frame.
    static constexpr std::int64_t kMaxDeltaMicroseconds = 1'000'000;

    /// Advances by `elapsed_microseconds` and returns how many ticks to run now.
    ///
    /// Never returns more than config::kMaxCatchupTicks. Time beyond that is
    /// dropped rather than queued: a host too slow to keep up must fall behind
    /// wall clock instead of accumulating a backlog it can never work through,
    /// which is what turns a stutter into a freeze.
    [[nodiscard]] constexpr int advance(std::int64_t elapsed_microseconds) noexcept {
        // A clock that went backwards contributes nothing rather than rewinding
        // the simulation.
        if (elapsed_microseconds <= 0) {
            return 0;
        }
        elapsed_microseconds = std::min(elapsed_microseconds, kMaxDeltaMicroseconds);

        accumulator_ += elapsed_microseconds * config::kTickHz;

        int ticks = 0;
        while (accumulator_ >= kUnitsPerTick && ticks < config::kMaxCatchupTicks) {
            accumulator_ -= kUnitsPerTick;
            ++ticks;
        }

        // Anything still owed beyond the cap is abandoned, but the sub-tick
        // remainder is kept so timing stays exact once the host recovers.
        if (accumulator_ >= kUnitsPerTick) {
            accumulator_ %= kUnitsPerTick;
        }

        return ticks;
    }

    /// Sub-tick remainder, in microsecond-ticks. Diagnostics only: nothing in
    /// the simulation may read this, or the tick would stop being fixed.
    [[nodiscard]] constexpr std::int64_t remainder() const noexcept { return accumulator_; }

    constexpr void reset() noexcept { accumulator_ = 0; }

private:
    std::int64_t accumulator_ = 0;
};

} // namespace wumpo::core
