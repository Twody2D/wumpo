#pragma once

#include "core/config.hpp"
#include "core/rng.hpp"
#include "input/button.hpp"
#include "input/input_state.hpp"
#include "renderer/framebuffer.hpp"

#include <cstdint>

namespace wumpo::game {

/// Wumpo's second game.
///
/// One sentence: you are a point in total darkness; a ping briefly lights up
/// the wall ahead, and you have to cross it from memory before the light
/// fades and it comes again. Move with Left and Right, ping with A.
///
/// See docs/game-design/echo.md for the design rationale.
class EchoGame {
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
    static constexpr int kWallHeight = 2;
    static constexpr int kMoveEveryTicks = 4; // player horizontal step, at 60 Hz

    /// Screen layout, part of the visible contract rather than a hidden
    /// implementation detail: rows above `kPlayfieldTop` are the ping-readiness
    /// bar, score and rule line; `kPlayerRow` is where the player always sits.
    static constexpr int kPlayfieldTop = 7;
    static constexpr int kPlayerRow = config::kScreenHeight - kPlayerHeight - 1;

    /// `initial_high_score` seeds the best score from a save file. It is not
    /// reset by `reset()` - a restart clears the run, not the record set
    /// before it - and it is excluded from `stateHash()`: a locally loaded
    /// save is not part of "the run" a replay reproduces, and including it
    /// would make the same replay hash differently on two machines with
    /// different saves.
    explicit EchoGame(std::uint64_t seed = 1, int initial_high_score = 0)
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

    /// The wall's current row - what would otherwise only be readable by
    /// scanning a rendered frame during a ping, useful to tests and a future
    /// debug overlay alike.
    [[nodiscard]] int wallRow() const noexcept { return wall_y_; }

    /// Ticks left before the wall lit by the last ping goes dark again. Zero
    /// outside a ping.
    [[nodiscard]] int pingVisibleTicks() const noexcept { return ping_visible_remaining_; }

    /// Ticks left before another ping can be triggered. Zero means ready.
    [[nodiscard]] int pingCooldownTicks() const noexcept { return ping_cooldown_remaining_; }

    /// FNV-1a over the fields that define a run. Replay and determinism tests
    /// compare this instead of poking at internals, so adding a field that does
    /// not affect play does not break them.
    [[nodiscard]] std::uint64_t stateHash() const noexcept;

private:
    void resetWall() noexcept;
    [[nodiscard]] bool playerClearsGap() const noexcept;

    core::Pcg32 rng_;
    std::uint64_t seed_ = 0;

    // Positions and periods are plain integers in device pixels and ticks: no
    // floats anywhere in the simulation, so this behaves identically on every
    // compiler and on an MCU with no FPU.
    int player_x_ = 0;
    int wall_y_ = 0;

    // Unlike The Shift, the gap does not slide in a readable pattern: each
    // wall draws a fresh offset from wherever the player currently stands, in
    // a random direction and a magnitude between kMinGapJump and kMaxGapJump
    // (see echo.cpp). Bounded above so it always stays physically reachable
    // in the time a wall takes to fall, however far the difficulty ramp has
    // shortened that time - an unbounded jump paired with a shrinking fall
    // budget is exactly the speed-mismatch bug The Shift had. Bounded below
    // so a jump can never be small enough to leave the player already
    // standing in the new gap by chance - that turned entire runs into a
    // score that climbed on its own, without the player doing anything.
    int gap_x_ = 0;
    int gap_width_ = 0;

    // Difficulty ramps by shortening the fall period and narrowing the gap as
    // the score grows.
    int fall_every_ticks_ = 0;
    int fall_countdown_ = 0;

    // A ping lights the current wall for a fixed window, then it is dark again
    // until the cooldown clears - the resource the player is really managing.
    int ping_visible_remaining_ = 0;
    int ping_cooldown_remaining_ = 0;

    int score_ = 0;
    int high_score_ = 0;
    int ticks_ = 0;
    Phase phase_ = Phase::Playing;

    // True from the first ping of this run onward. Until then, render() shows
    // a hint in the playfield - the whole mechanic is invisible until you
    // press A once, so the darkness itself must not be the first thing a new
    // player is left alone with.
    bool has_pinged_ = false;
};

} // namespace wumpo::game
