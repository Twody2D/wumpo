#pragma once

#include "core/config.hpp"
#include "core/rng.hpp"
#include "input/button.hpp"
#include "input/input_state.hpp"
#include "renderer/framebuffer.hpp"

#include <cstdint>

namespace wumpo::game {

/// Wumpo's first real game.
///
/// One sentence: a wall with a gap falls toward you, and once every beat the
/// gap slides sideways by a fixed step - so surviving is about predicting
/// where it will be, not just reacting to where it is now. Move with Left and
/// Right only; nothing else is needed.
///
/// See docs/game-design/the-shift.md for the design rationale.
class ShiftGame {
public:
    enum class Phase : std::uint8_t { Playing, Over };

    /// A tone the game wants played. Returned rather than played directly so
    /// the simulation stays free of side effects and stays testable headless.
    struct Sound {
        std::uint16_t frequency_hz = 0;
        std::uint16_t duration_ms = 0;

        [[nodiscard]] constexpr bool silent() const noexcept { return duration_ms == 0; }
    };

    static constexpr int kPlayerWidth = 3;
    static constexpr int kPlayerHeight = 2;
    static constexpr int kGapWidth = 9;
    static constexpr int kWallHeight = 2;
    static constexpr int kMoveEveryTicks = 4; // player horizontal step, at 60 Hz

    /// Screen layout, part of the visible contract rather than a hidden
    /// implementation detail: rows above `kPlayfieldTop` are the beat bar,
    /// score and rule line; `kPlayerRow` is where the player always sits.
    /// Exposed so tests can find the wall and player by scanning a rendered
    /// frame instead of reaching into private state.
    static constexpr int kPlayfieldTop = 7;
    static constexpr int kPlayerRow = config::kScreenHeight - kPlayerHeight - 1;

    /// `initial_high_score` seeds the best score from a save file. It is not
    /// reset by `reset()` - a restart clears the run, not the record set
    /// before it - and it is excluded from `stateHash()`: a locally loaded
    /// save is not part of "the run" a replay reproduces, and including it
    /// would make the same replay hash differently on two machines with
    /// different saves.
    explicit ShiftGame(std::uint64_t seed = 1, int initial_high_score = 0)
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

    /// The wall's current row - a number for what is otherwise only readable
    /// by scanning the rendered frame, useful to tests and a future debug
    /// overlay alike.
    [[nodiscard]] int wallRow() const noexcept { return wall_y_; }

    /// FNV-1a over the fields that define a run. Replay and determinism tests
    /// compare this instead of poking at internals, so adding a field that does
    /// not affect play does not break them.
    [[nodiscard]] std::uint64_t stateHash() const noexcept;

private:
    void resetWall() noexcept;
    void beginShift() noexcept;
    [[nodiscard]] bool playerClearsGap() const noexcept;

    core::Pcg32 rng_;
    std::uint64_t seed_ = 0;

    // Positions and periods are plain integers in device pixels and ticks: no
    // floats anywhere in the simulation, so this behaves identically on every
    // compiler and on an MCU with no FPU.
    int player_x_ = 0;
    int wall_y_ = 0;

    // The gap slides one pixel per tick toward gap_target_x_ rather than
    // jumping there in one frame: an instant jump gives the eye nothing to
    // read a direction from, which defeats a game about predicting a
    // movement. gap_direction_ is the direction the *next* shift will try to
    // continue in, before edge reflection.
    int gap_x_ = 0;
    int gap_target_x_ = 0;
    int gap_direction_ = 1; // +1 or -1
    int slide_ticks_remaining_ = 0;

    // Difficulty ramps by shortening these two periods as the score grows.
    int fall_every_ticks_ = 0;
    int shift_period_ticks_ = 0;
    int fall_countdown_ = 0;
    int shift_countdown_ = 0;

    int score_ = 0;
    int high_score_ = 0;
    int ticks_ = 0;
    Phase phase_ = Phase::Playing;
};

} // namespace wumpo::game
