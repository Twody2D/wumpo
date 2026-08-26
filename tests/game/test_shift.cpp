#include "game/shift.hpp"
#include "golden_support.hpp"
#include "input/button.hpp"
#include "input/input_state.hpp"
#include "renderer/framebuffer.hpp"

#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

using wumpo::game::ShiftGame;
using wumpo::input::Button;
using wumpo::input::ButtonMask;
using wumpo::input::InputState;
using wumpo::renderer::Framebuffer;
namespace input = wumpo::input;
namespace config = wumpo::config;

namespace {

/// Runs a game for a sequence of held-masks, exactly as the frame loop would.
/// One mask per tick, edges derived by the runtime - the same path a replay
/// takes.
std::uint64_t run(ShiftGame& game, const std::vector<ButtonMask>& masks) {
    InputState state;
    for (const ButtonMask mask : masks) {
        state.update(mask);
        (void)game.tick(state);
    }
    return game.stateHash();
}

std::vector<ButtonMask> heldFor(ButtonMask mask, int ticks) {
    return std::vector<ButtonMask>(static_cast<std::size_t>(ticks), mask);
}

struct GapSpan {
    int start = -1;
    int width = 0;
};

/// Finds the wall's gap by scanning a rendered row for the unlit run in an
/// otherwise-lit wall - the same "read the frame, don't peek at private
/// state" approach the framebuffer bounds tests use elsewhere in this suite.
GapSpan findGap(const Framebuffer& frame, int row) {
    GapSpan gap;
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
        if (!frame.pixel(x, row)) {
            if (gap.start < 0) {
                gap.start = x;
            }
            ++gap.width;
        }
    }
    return gap;
}

int gapClearTarget(const GapSpan& gap) {
    return gap.start + (gap.width - ShiftGame::kPlayerWidth) / 2;
}

/// A position guaranteed not to overlap `gap`, with a pixel of margin on
/// whichever side has room - the gap is far narrower than the screen, so one
/// side always does.
int guaranteedMissTarget(const GapSpan& gap) {
    int target = gap.start - ShiftGame::kPlayerWidth - 1;
    if (target < 0) {
        target = gap.start + gap.width + 1;
    }
    return std::clamp(target, 0, config::kScreenWidth - ShiftGame::kPlayerWidth);
}

/// Moves the player from `from_x` to `target_x` using exactly the ticks
/// `ShiftGame::tick`'s own movement rule needs (one pixel every
/// `kMoveEveryTicks` ticks), then leaves input released. Assumes the game
/// starts fresh, so this is the only mover in play up to this point.
void driveTo(ShiftGame& game, InputState& state, int from_x, int target_x) {
    const int distance = target_x - from_x;
    if (distance == 0) {
        return;
    }
    const ButtonMask mask =
        distance > 0 ? input::maskOf(Button::Right) : input::maskOf(Button::Left);
    const int steps = std::abs(distance) * ShiftGame::kMoveEveryTicks;
    for (int i = 0; i < steps; ++i) {
        state.update(mask);
        (void)game.tick(state);
    }
}

/// Ticks with no input held until the current wall resolves (score changes or
/// the run ends), for at most `max_ticks`.
void holdUntilResolved(ShiftGame& game, InputState& state, int max_ticks) {
    const int score_before = game.score();
    for (int i = 0;
         i < max_ticks && game.phase() == ShiftGame::Phase::Playing && game.score() == score_before;
         ++i) {
        state.update(0);
        (void)game.tick(state);
    }
}

/// Continuously steers toward the middle of whatever the current gap is, the
/// way a player tracking the beat would, rather than aiming once at a
/// precomputed spot: the gap drifts slowly enough that this always keeps up,
/// regardless of how far a fall has left to go or where the player starts.
///
/// `current_x` is tracked by mirroring `ShiftGame::tick`'s own quantised
/// movement rule instead of being read back from the frame: for the one fall
/// step where the two-row-tall wall reaches the player's row a tick before
/// the collision check fires, the wall and the player are drawn on the same
/// row, and scanning for "the lit span on the player's row" can find the
/// wall's lit segment instead.
void trackGapUntilResolved(ShiftGame& game, InputState& state, Framebuffer& frame, int& current_x,
                           int max_ticks) {
    const int score_before = game.score();
    for (int i = 0;
         i < max_ticks && game.phase() == ShiftGame::Phase::Playing && game.score() == score_before;
         ++i) {
        game.render(frame);
        const auto gap = findGap(frame, game.wallRow());
        const int target = gapClearTarget(gap);

        ButtonMask mask = 0;
        if (current_x < target) {
            mask = input::maskOf(Button::Right);
        } else if (current_x > target) {
            mask = input::maskOf(Button::Left);
        }
        state.update(mask);
        (void)game.tick(state);

        if (game.tickCount() % ShiftGame::kMoveEveryTicks == 0) {
            if (mask == input::maskOf(Button::Right)) {
                current_x = std::min(current_x + 1, config::kScreenWidth - ShiftGame::kPlayerWidth);
            } else if (mask == input::maskOf(Button::Left)) {
                current_x = std::max(current_x - 1, 0);
            }
        }
    }
}

/// Drives a fresh game (score 0, player centred) into a guaranteed crash.
void crashTheGame(ShiftGame& game) {
    Framebuffer frame;
    game.render(frame);
    const auto gap = findGap(frame, game.wallRow());
    const int target_x = guaranteedMissTarget(gap);

    InputState state;
    const int start_x = (config::kScreenWidth - ShiftGame::kPlayerWidth) / 2;
    driveTo(game, state, start_x, target_x);
    holdUntilResolved(game, state, 400);
}

} // namespace

TEST_SUITE("game") {

    TEST_CASE("a new run starts playing with nothing scored") {
        const ShiftGame game(1);
        CHECK(game.score() == 0);
        CHECK(game.tickCount() == 0);
        CHECK(game.phase() == ShiftGame::Phase::Playing);
        CHECK(game.seed() == 1);
        CHECK(game.highScore() == 0);
    }

    TEST_CASE("the same seed and inputs produce the same final state") {
        // The property every replay, golden test and future leaderboard rests on.
        const auto script = heldFor(input::maskOf(Button::Right), 200);

        ShiftGame first(4242);
        ShiftGame second(4242);
        CHECK(run(first, script) == run(second, script));
        CHECK(first.score() == second.score());
    }

    TEST_CASE("different seeds produce different runs") {
        const auto script = heldFor(input::maskOf(Button::Right), 120);
        ShiftGame first(1);
        ShiftGame second(2);
        CHECK(run(first, script) != run(second, script));
    }

    TEST_CASE("different inputs diverge from the same seed") {
        ShiftGame left(9);
        ShiftGame right(9);
        CHECK(run(left, heldFor(input::maskOf(Button::Left), 60)) !=
              run(right, heldFor(input::maskOf(Button::Right), 60)));
    }

    TEST_CASE("the player is clamped to the left edge") {
        // 124 ticks covers the 120 needed to cross from centre to x=0 (30
        // steps at one pixel per kMoveEveryTicks), with room to spare before
        // the first wall can possibly resolve at tick 132.
        ShiftGame game(1);
        InputState state;
        for (int i = 0; i < 124; ++i) {
            state.update(input::maskOf(Button::Left));
            (void)game.tick(state);
        }
        REQUIRE(game.phase() == ShiftGame::Phase::Playing);
        Framebuffer frame;
        game.render(frame);
        CHECK(frame.pixel(0, ShiftGame::kPlayerRow));
        CHECK_FALSE(frame.pixel(ShiftGame::kPlayerWidth, ShiftGame::kPlayerRow));
    }

    TEST_CASE("the player is clamped to the right edge") {
        ShiftGame game(1);
        InputState state;
        for (int i = 0; i < 124; ++i) {
            state.update(input::maskOf(Button::Right));
            (void)game.tick(state);
        }
        REQUIRE(game.phase() == ShiftGame::Phase::Playing);
        Framebuffer frame;
        game.render(frame);
        const int right_edge = config::kScreenWidth - 1;
        CHECK(frame.pixel(right_edge, ShiftGame::kPlayerRow));
        CHECK(frame.pixel(right_edge - ShiftGame::kPlayerWidth + 1, ShiftGame::kPlayerRow));
    }

    TEST_CASE("tracking the gap keeps scoring, and repeated passes speed up the fall") {
        ShiftGame game(7);
        InputState state;
        Framebuffer frame;
        int current_x = (config::kScreenWidth - ShiftGame::kPlayerWidth) / 2;

        // Twenty passes runs well past every difficulty step (every third
        // point, floor reached at score 12): a speed mismatch between the
        // gap's slide and the player's own top speed would show up as an
        // unwinnable pass long before this, the way it did when the slide
        // moved faster than the player physically could.
        int previous_duration = -1;
        for (int pass = 0; pass < 20; ++pass) {
            const int ticks_before = game.tickCount();
            trackGapUntilResolved(game, state, frame, current_x, 500);
            REQUIRE(game.phase() == ShiftGame::Phase::Playing);
            REQUIRE(game.score() == pass + 1);

            const int duration = game.tickCount() - ticks_before;
            // Difficulty only ramps every few points, so duration is
            // non-increasing rather than strictly shorter every single pass.
            if (previous_duration > 0) {
                CHECK(duration <= previous_duration);
            }
            previous_duration = duration;
        }
        CHECK(game.score() == 20);
        CHECK(game.highScore() == 20);
    }

    TEST_CASE("missing the gap ends the run and does not score") {
        ShiftGame game(3);
        crashTheGame(game);
        CHECK(game.phase() == ShiftGame::Phase::Over);
        CHECK(game.score() == 0);
    }

    TEST_CASE("the run ends with a sound") {
        ShiftGame game(3);
        Framebuffer frame;
        game.render(frame);
        const auto gap = findGap(frame, game.wallRow());
        const int target_x = guaranteedMissTarget(gap);

        InputState state;
        const int start_x = (config::kScreenWidth - ShiftGame::kPlayerWidth) / 2;
        driveTo(game, state, start_x, target_x);

        ShiftGame::Sound last;
        for (int i = 0; i < 400 && game.phase() == ShiftGame::Phase::Playing; ++i) {
            state.update(0);
            last = game.tick(state);
        }
        REQUIRE(game.phase() == ShiftGame::Phase::Over);
        CHECK_FALSE(last.silent());
    }

    TEST_CASE("A restarts after a crash, holding it does not restart twice, and the "
              "high score survives") {
        ShiftGame game(3);
        crashTheGame(game);
        REQUIRE(game.phase() == ShiftGame::Phase::Over);
        const int high_score_before = game.highScore();

        InputState state;
        state.update(input::maskOf(Button::A)); // press edge
        (void)game.tick(state);
        CHECK(game.phase() == ShiftGame::Phase::Playing);
        CHECK(game.score() == 0);
        CHECK(game.tickCount() == 0);
        CHECK(game.highScore() == high_score_before);

        // Still holding A: the run must continue, not restart every tick.
        state.update(input::maskOf(Button::A));
        (void)game.tick(state);
        CHECK(game.tickCount() == 1);
    }

    TEST_CASE("input during play does not restart anything") {
        ShiftGame game(1);
        InputState state;
        state.update(input::maskOf(Button::A));
        (void)game.tick(state);
        CHECK(game.phase() == ShiftGame::Phase::Playing);
        CHECK(game.tickCount() == 1);
    }

    TEST_CASE("rendering does not change state") {
        // Golden tests are only meaningful if drawing is a pure function of state.
        ShiftGame game(77);
        (void)run(game, heldFor(input::maskOf(Button::Right), 50));
        const std::uint64_t before = game.stateHash();

        Framebuffer first;
        Framebuffer second;
        game.render(first);
        game.render(second);

        CHECK(game.stateHash() == before);
        CHECK(first == second);
    }

} // TEST_SUITE

TEST_SUITE("golden") {

    TEST_CASE("the opening frame") {
        const ShiftGame game(12345);
        Framebuffer frame;
        game.render(frame);
        wumpo::testing::checkGolden("shift-opening", frame);
    }

    TEST_CASE("mid-run frame") {
        ShiftGame game(12345);
        InputState state;
        for (int i = 0; i < 60; ++i) {
            state.update(i < 30 ? input::maskOf(Button::Right) : input::maskOf(Button::Left));
            (void)game.tick(state);
        }
        Framebuffer frame;
        game.render(frame);
        wumpo::testing::checkGolden("shift-midrun", frame);
    }

    TEST_CASE("the crash screen") {
        ShiftGame game(3);
        crashTheGame(game);
        REQUIRE(game.phase() == ShiftGame::Phase::Over);
        Framebuffer frame;
        game.render(frame);
        wumpo::testing::checkGolden("shift-gameover", frame);
    }

} // TEST_SUITE
