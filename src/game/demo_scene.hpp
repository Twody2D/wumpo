#pragma once

#include "core/config.hpp"
#include "core/rng.hpp"
#include "input/button.hpp"
#include "input/input_state.hpp"
#include "renderer/framebuffer.hpp"

#include <cstdint>

namespace wumpo::game {

/// Not a game - a proof that every layer is wired together correctly.
///
/// It exercises exactly the things that are hard to verify from tests alone:
/// input reaches the simulation, the simulation is integer-only and
/// deterministic, rendering fills the screen the way the device would, sound
/// fires on an event, and the whole thing runs at a fixed tick rate. When the
/// first real prototype arrives, this is deleted.
///
/// Rules, such as they are: you move a cursor; a target appears; touching it
/// scores and moves the target. There is a timer, and when it runs out the run
/// is over. It is not meant to be fun, and it is not a candidate mechanic.
class DemoScene {
public:
    enum class Phase : std::uint8_t { Playing, Over };

    /// A tone the scene wants played. Returned rather than played directly so
    /// the simulation stays free of side effects and stays testable headless.
    struct Sound {
        std::uint16_t frequency_hz = 0;
        std::uint16_t duration_ms = 0;

        [[nodiscard]] constexpr bool silent() const noexcept { return duration_ms == 0; }
    };

    static constexpr int kRunTicks = config::kTickHz * 30; // half a minute
    static constexpr int kPlayerSize = 3;
    static constexpr int kTargetSize = 3;

    /// `initial_high_score` seeds the best score from a save file. It is not
    /// reset by `reset()`: a restart clears the run, not the record set before
    /// it. It is also excluded from `stateHash()` - a locally loaded save is not
    /// part of "the run" a replay reproduces, and including it would make the
    /// same replay hash differently on two machines with different saves.
    explicit DemoScene(std::uint64_t seed = 1, int initial_high_score = 0)
        : high_score_(initial_high_score) {
        reset(seed);
    }

    void reset(std::uint64_t seed);

    /// Advances one tick. Takes input and nothing else - no delta time, no
    /// clock - which is what makes a replay of the same masks reproduce this
    /// exactly. Returns a sound to play, if anything happened worth hearing.
    [[nodiscard]] Sound tick(const input::InputState& input);

    /// Draws the current state. Must not modify anything: golden tests rely on
    /// the same state always producing the same frame.
    void render(renderer::Framebuffer& frame) const;

    [[nodiscard]] int score() const noexcept { return score_; }
    [[nodiscard]] int tickCount() const noexcept { return ticks_; }
    [[nodiscard]] Phase phase() const noexcept { return phase_; }
    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }
    [[nodiscard]] int highScore() const noexcept { return high_score_; }

    /// FNV-1a over the fields that define a run. Replay and determinism tests
    /// compare this instead of poking at internals, so adding a field that does
    /// not affect play does not break them.
    [[nodiscard]] std::uint64_t stateHash() const noexcept;

private:
    void placeTarget();
    [[nodiscard]] bool touchingTarget() const noexcept;

    core::Pcg32 rng_;
    std::uint64_t seed_ = 0;

    // Positions are plain integers in device pixels: no floats anywhere in the
    // simulation, so this behaves identically on every compiler and on an MCU
    // with no FPU.
    int player_x_ = 0;
    int player_y_ = 0;
    int target_x_ = 0;
    int target_y_ = 0;

    int score_ = 0;
    int high_score_ = 0;
    int ticks_ = 0;
    int ticks_left_ = 0;
    Phase phase_ = Phase::Playing;
};

} // namespace wumpo::game
