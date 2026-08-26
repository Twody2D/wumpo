#include "game/echo.hpp"
#include "golden_support.hpp"
#include "input/button.hpp"
#include "input/input_state.hpp"
#include "renderer/framebuffer.hpp"

#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

using wumpo::game::EchoGame;
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
std::uint64_t run(EchoGame& game, const std::vector<ButtonMask>& masks) {
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
/// otherwise-lit wall. Only meaningful while a ping has the wall lit - an
/// unlit wall renders nothing at all, so this must be called right after a
/// tick where `pingVisibleTicks() > 0`.
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

bool rowHasLitPixel(const Framebuffer& frame, int row) {
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
        if (frame.pixel(x, row)) {
            return true;
        }
    }
    return false;
}

int gapClearTarget(const GapSpan& gap) {
    return gap.start + (gap.width - EchoGame::kPlayerWidth) / 2;
}

/// A position guaranteed not to overlap `gap`, with a pixel of margin on
/// whichever side has room - the gap is far narrower than the screen, so one
/// side always does.
int guaranteedMissTarget(const GapSpan& gap) {
    int target = gap.start - EchoGame::kPlayerWidth - 1;
    if (target < 0) {
        target = gap.start + gap.width + 1;
    }
    return std::clamp(target, 0, config::kScreenWidth - EchoGame::kPlayerWidth);
}

/// Moves the player from `from_x` to `target_x` using exactly the ticks
/// `EchoGame::tick`'s own movement rule needs (one pixel every
/// `kMoveEveryTicks` ticks), then leaves input released.
void driveTo(EchoGame& game, InputState& state, int from_x, int target_x) {
    const int distance = target_x - from_x;
    if (distance == 0) {
        return;
    }
    const ButtonMask mask =
        distance > 0 ? input::maskOf(Button::Right) : input::maskOf(Button::Left);
    const int steps = std::abs(distance) * EchoGame::kMoveEveryTicks;
    for (int i = 0; i < steps; ++i) {
        state.update(mask);
        (void)game.tick(state);
    }
}

/// Ticks with no input held until the current wall resolves (score changes or
/// the run ends), for at most `max_ticks`.
void holdUntilResolved(EchoGame& game, InputState& state, int max_ticks) {
    const int score_before = game.score();
    for (int i = 0;
         i < max_ticks && game.phase() == EchoGame::Phase::Playing && game.score() == score_before;
         ++i) {
        state.update(0);
        (void)game.tick(state);
    }
}

/// Pings once to learn the current wall's gap (unlike The Shift, the gap does
/// not move once drawn, so one look is enough), then walks to its centre and
/// waits for the wall to land. `current_x` is tracked by mirroring
/// `EchoGame::tick`'s own quantised movement rule rather than re-scanning the
/// frame every tick, the same reason test_shift.cpp does this: the two-row
/// wall can visually reach the player's row a tick before the collision check
/// actually fires.
void crossGapUsingOnePing(EchoGame& game, InputState& state, Framebuffer& frame, int& current_x,
                          int max_ticks) {
    const int score_before = game.score();

    if (game.pingCooldownTicks() <= 0) {
        state.update(input::maskOf(Button::A));
        (void)game.tick(state);
        --max_ticks;
    }
    game.render(frame);
    const auto gap = findGap(frame, game.wallRow());
    const int target = gapClearTarget(gap);

    for (int i = 0;
         i < max_ticks && game.phase() == EchoGame::Phase::Playing && game.score() == score_before;
         ++i) {
        ButtonMask mask = 0;
        if (current_x < target) {
            mask = input::maskOf(Button::Right);
        } else if (current_x > target) {
            mask = input::maskOf(Button::Left);
        }
        state.update(mask);
        (void)game.tick(state);

        if (game.tickCount() % EchoGame::kMoveEveryTicks == 0) {
            if (mask == input::maskOf(Button::Right)) {
                current_x = std::min(current_x + 1, config::kScreenWidth - EchoGame::kPlayerWidth);
            } else if (mask == input::maskOf(Button::Left)) {
                current_x = std::max(current_x - 1, 0);
            }
        }
    }
}

/// Drives a fresh game (score 0, player centred) into a guaranteed crash: a
/// ping reveals the gap, then the player deliberately walks somewhere else.
void crashTheGame(EchoGame& game) {
    InputState state;
    Framebuffer frame;

    state.update(input::maskOf(Button::A));
    (void)game.tick(state);
    game.render(frame);
    const auto gap = findGap(frame, game.wallRow());
    const int target_x = guaranteedMissTarget(gap);

    const int start_x = (config::kScreenWidth - EchoGame::kPlayerWidth) / 2;
    driveTo(game, state, start_x, target_x);
    holdUntilResolved(game, state, 400);
}

} // namespace

TEST_SUITE("game") {

    TEST_CASE("a new run starts playing with nothing scored") {
        const EchoGame game(1);
        CHECK(game.score() == 0);
        CHECK(game.tickCount() == 0);
        CHECK(game.phase() == EchoGame::Phase::Playing);
        CHECK(game.seed() == 1);
        CHECK(game.highScore() == 0);
        CHECK(game.pingVisibleTicks() == 0);
        CHECK(game.pingCooldownTicks() == 0);
    }

    TEST_CASE("the same seed and inputs produce the same final state") {
        // The property every replay, golden test and future leaderboard rests on.
        const auto script = heldFor(input::maskOf(Button::Right), 200);

        EchoGame first(4242);
        EchoGame second(4242);
        CHECK(run(first, script) == run(second, script));
        CHECK(first.score() == second.score());
    }

    TEST_CASE("different seeds produce different runs") {
        const auto script = heldFor(input::maskOf(Button::Right), 120);
        EchoGame first(1);
        EchoGame second(2);
        CHECK(run(first, script) != run(second, script));
    }

    TEST_CASE("different inputs diverge from the same seed") {
        EchoGame left(9);
        EchoGame right(9);
        CHECK(run(left, heldFor(input::maskOf(Button::Left), 60)) !=
              run(right, heldFor(input::maskOf(Button::Right), 60)));
    }

    TEST_CASE("the player is clamped to the left edge") {
        // 124 ticks covers the 120 needed to cross from centre to x=0 (30
        // steps at one pixel per kMoveEveryTicks), with room to spare before
        // the first wall can possibly resolve (22 falls at the initial 8
        // ticks each, at tick 176).
        EchoGame game(1);
        InputState state;
        for (int i = 0; i < 124; ++i) {
            state.update(input::maskOf(Button::Left));
            (void)game.tick(state);
        }
        REQUIRE(game.phase() == EchoGame::Phase::Playing);
        Framebuffer frame;
        game.render(frame);
        CHECK(frame.pixel(0, EchoGame::kPlayerRow));
        CHECK_FALSE(frame.pixel(EchoGame::kPlayerWidth, EchoGame::kPlayerRow));
    }

    TEST_CASE("the player is clamped to the right edge") {
        EchoGame game(1);
        InputState state;
        for (int i = 0; i < 124; ++i) {
            state.update(input::maskOf(Button::Right));
            (void)game.tick(state);
        }
        REQUIRE(game.phase() == EchoGame::Phase::Playing);
        Framebuffer frame;
        game.render(frame);
        const int right_edge = config::kScreenWidth - 1;
        CHECK(frame.pixel(right_edge, EchoGame::kPlayerRow));
        CHECK(frame.pixel(right_edge - EchoGame::kPlayerWidth + 1, EchoGame::kPlayerRow));
    }

    TEST_CASE("a ping lights the wall for a while and then goes dark again") {
        EchoGame game(1);
        InputState state;
        state.update(input::maskOf(Button::A));
        (void)game.tick(state);
        REQUIRE(game.pingVisibleTicks() > 0);

        Framebuffer frame;
        game.render(frame);
        const auto lit = findGap(frame, game.wallRow());
        CHECK(lit.start >= 0); // the wall drew something with a gap in it

        const int visible_ticks = game.pingVisibleTicks();
        for (int i = 0; i < visible_ticks + 1; ++i) {
            state.update(0);
            (void)game.tick(state);
        }
        CHECK(game.pingVisibleTicks() == 0);
        game.render(frame);
        // Dark again: the wall's row draws nothing at all, so the whole row
        // scans as one continuous "gap" the width of the screen.
        const auto dark = findGap(frame, game.wallRow());
        CHECK(dark.width == config::kScreenWidth);
    }

    TEST_CASE("a player who never moves never scores by accident") {
        // The bug this guards against: a gap jump small enough to leave the
        // player already standing inside the new gap scored runs on their
        // own, without the player doing anything - "nothing is visible, but
        // the counter keeps going up". A stationary player must always
        // eventually miss.
        for (std::uint64_t seed = 1; seed <= 20; ++seed) {
            EchoGame game(seed);
            InputState state;
            for (int tick = 0; tick < 400 && game.phase() == EchoGame::Phase::Playing; ++tick) {
                state.update(0);
                (void)game.tick(state);
            }
            CHECK(game.phase() == EchoGame::Phase::Over);
            CHECK(game.score() == 0);
        }
    }

    TEST_CASE("the press-A hint shows before the first ping of a run and never after") {
        // Mirrors echo.cpp's kHintRow: clear of both the player's row and the
        // wall's opening position, so nothing else could light it up instead.
        constexpr int kHintRow = 17;

        EchoGame game(1);
        Framebuffer frame;
        game.render(frame);
        CHECK(rowHasLitPixel(frame, kHintRow));

        InputState state;
        state.update(input::maskOf(Button::A));
        (void)game.tick(state);
        game.render(frame);
        CHECK_FALSE(rowHasLitPixel(frame, kHintRow));

        // Stays gone even once the ping itself has faded back to darkness.
        const int visible_ticks = game.pingVisibleTicks();
        for (int i = 0; i < visible_ticks + 1; ++i) {
            state.update(0);
            (void)game.tick(state);
        }
        game.render(frame);
        CHECK_FALSE(rowHasLitPixel(frame, kHintRow));
    }

    TEST_CASE("a ping cannot be re-triggered before its cooldown clears") {
        EchoGame game(1);
        InputState state;
        state.update(input::maskOf(Button::A));
        (void)game.tick(state);
        const int cooldown_after_first_ping = game.pingCooldownTicks();
        REQUIRE(cooldown_after_first_ping > 0);

        state.update(input::maskOf(Button::A)); // held, not a new press edge anyway
        (void)game.tick(state);
        CHECK(game.pingCooldownTicks() == cooldown_after_first_ping - 1);
    }

    TEST_CASE("crossing the gap by memory after a single ping keeps scoring, and repeated "
              "passes tighten the gap without ever becoming unwinnable") {
        EchoGame game(7);
        InputState state;
        Framebuffer frame;
        int current_x = (config::kScreenWidth - EchoGame::kPlayerWidth) / 2;

        // Twenty passes runs well past every difficulty step (every third
        // point, floor reached at score 12): the gap's jump is bounded to
        // kMaxGapJump specifically so this never becomes physically
        // unreachable once the fall speed hits its floor - the same class of
        // bug The Shift had before its slide was capped to the player's speed.
        for (int pass = 0; pass < 20; ++pass) {
            crossGapUsingOnePing(game, state, frame, current_x, 500);
            REQUIRE(game.phase() == EchoGame::Phase::Playing);
            REQUIRE(game.score() == pass + 1);
        }
        CHECK(game.score() == 20);
        CHECK(game.highScore() == 20);
    }

    TEST_CASE("missing the gap ends the run and does not score") {
        EchoGame game(3);
        crashTheGame(game);
        CHECK(game.phase() == EchoGame::Phase::Over);
        CHECK(game.score() == 0);
    }

    TEST_CASE("the run ends with a sound") {
        EchoGame game(3);
        InputState state;
        Framebuffer frame;

        state.update(input::maskOf(Button::A));
        (void)game.tick(state);
        game.render(frame);
        const auto gap = findGap(frame, game.wallRow());
        const int target_x = guaranteedMissTarget(gap);

        const int start_x = (config::kScreenWidth - EchoGame::kPlayerWidth) / 2;
        driveTo(game, state, start_x, target_x);

        EchoGame::Sound last;
        for (int i = 0; i < 400 && game.phase() == EchoGame::Phase::Playing; ++i) {
            state.update(0);
            last = game.tick(state);
        }
        REQUIRE(game.phase() == EchoGame::Phase::Over);
        CHECK_FALSE(last.silent());
    }

    TEST_CASE("A restarts after a crash, holding it does not restart twice, and the "
              "high score survives") {
        EchoGame game(3);
        crashTheGame(game);
        REQUIRE(game.phase() == EchoGame::Phase::Over);
        const int high_score_before = game.highScore();

        InputState state;
        state.update(input::maskOf(Button::A)); // press edge
        (void)game.tick(state);
        CHECK(game.phase() == EchoGame::Phase::Playing);
        CHECK(game.score() == 0);
        CHECK(game.tickCount() == 0);
        CHECK(game.highScore() == high_score_before);

        // Still holding A: the run must continue, not restart every tick.
        state.update(input::maskOf(Button::A));
        (void)game.tick(state);
        CHECK(game.tickCount() == 1);
    }

    TEST_CASE("input during play does not restart anything") {
        EchoGame game(1);
        InputState state;
        state.update(input::maskOf(Button::A));
        (void)game.tick(state);
        CHECK(game.phase() == EchoGame::Phase::Playing);
        CHECK(game.tickCount() == 1);
    }

    TEST_CASE("rendering does not change state") {
        // Golden tests are only meaningful if drawing is a pure function of state.
        EchoGame game(77);
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

    TEST_CASE("echo opening frame") {
        const EchoGame game(12345);
        Framebuffer frame;
        game.render(frame);
        wumpo::testing::checkGolden("echo-opening", frame);
    }

    TEST_CASE("echo mid-run frame, mid-ping") {
        EchoGame game(12345);
        InputState state;
        state.update(input::maskOf(Button::A));
        (void)game.tick(state);
        for (int i = 0; i < 10; ++i) {
            state.update(i < 5 ? input::maskOf(Button::Right) : input::maskOf(Button::Left));
            (void)game.tick(state);
        }
        Framebuffer frame;
        game.render(frame);
        wumpo::testing::checkGolden("echo-midrun", frame);
    }

    TEST_CASE("echo crash screen") {
        EchoGame game(3);
        crashTheGame(game);
        REQUIRE(game.phase() == EchoGame::Phase::Over);
        Framebuffer frame;
        game.render(frame);
        wumpo::testing::checkGolden("echo-gameover", frame);
    }

} // TEST_SUITE
