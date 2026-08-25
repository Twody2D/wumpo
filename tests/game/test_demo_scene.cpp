#include "game/demo_scene.hpp"
#include "golden_support.hpp"
#include "input/button.hpp"
#include "input/input_state.hpp"
#include "renderer/framebuffer.hpp"

#include <doctest.h>

#include <cstdint>
#include <vector>

using wumpo::game::DemoScene;
using wumpo::input::Button;
using wumpo::input::ButtonMask;
using wumpo::input::InputState;
using wumpo::renderer::Framebuffer;
namespace input = wumpo::input;

namespace {

/// Runs a scene for a sequence of held-masks, exactly as the frame loop would.
/// One mask per tick, edges derived by the runtime - the same path a replay
/// takes.
std::uint64_t run(DemoScene& scene, const std::vector<ButtonMask>& masks) {
    InputState state;
    for (const ButtonMask mask : masks) {
        state.update(mask);
        (void)scene.tick(state);
    }
    return scene.stateHash();
}

std::vector<ButtonMask> heldFor(ButtonMask mask, int ticks) {
    return std::vector<ButtonMask>(static_cast<std::size_t>(ticks), mask);
}

} // namespace

TEST_SUITE("game") {

    TEST_CASE("a new run starts playing with nothing scored") {
        const DemoScene scene(1);
        CHECK(scene.score() == 0);
        CHECK(scene.tickCount() == 0);
        CHECK(scene.phase() == DemoScene::Phase::Playing);
        CHECK(scene.seed() == 1);
    }

    TEST_CASE("the same seed and inputs produce the same final state") {
        // The property every replay, golden test and future leaderboard rests on.
        const auto script = heldFor(input::maskOf(Button::Right), 200);

        DemoScene first(4242);
        DemoScene second(4242);
        CHECK(run(first, script) == run(second, script));
        CHECK(first.score() == second.score());
    }

    TEST_CASE("different seeds place targets differently") {
        const auto script = heldFor(input::maskOf(Button::Right), 120);
        DemoScene first(1);
        DemoScene second(2);
        CHECK(run(first, script) != run(second, script));
    }

    TEST_CASE("different inputs diverge from the same seed") {
        DemoScene left(9);
        DemoScene right(9);
        CHECK(run(left, heldFor(input::maskOf(Button::Left), 60)) !=
              run(right, heldFor(input::maskOf(Button::Right), 60)));
    }

    TEST_CASE("the player cannot leave the screen") {
        // Held hard against each edge for far longer than it takes to reach it.
        DemoScene scene(1);
        (void)run(scene, heldFor(input::maskOf(Button::Left), 400));
        (void)run(scene, heldFor(input::maskOf(Button::Up), 400));

        Framebuffer frame;
        scene.render(frame);
        // Something is still drawn: the player has not wandered out of the buffer,
        // which clipping would hide by silently discarding it.
        int lit = 0;
        for (int y = 0; y < Framebuffer::kHeight; ++y) {
            for (int x = 0; x < Framebuffer::kWidth; ++x) {
                lit += frame.pixel(x, y) ? 1 : 0;
            }
        }
        CHECK(lit > 0);
    }

    TEST_CASE("the run ends when the timer expires") {
        DemoScene scene(1);
        (void)run(scene, heldFor(0, DemoScene::kRunTicks));
        CHECK(scene.phase() == DemoScene::Phase::Over);
        CHECK(scene.tickCount() == DemoScene::kRunTicks);
    }

    TEST_CASE("the run ends with a sound") {
        DemoScene scene(1);
        InputState state;
        DemoScene::Sound last;
        for (int i = 0; i < DemoScene::kRunTicks; ++i) {
            state.update(0);
            last = scene.tick(state);
        }
        CHECK_FALSE(last.silent());
    }

    TEST_CASE("A restarts after game over, and holding it does not restart twice") {
        DemoScene scene(1);
        (void)run(scene, heldFor(0, DemoScene::kRunTicks));
        REQUIRE(scene.phase() == DemoScene::Phase::Over);

        InputState state;
        state.update(input::maskOf(Button::A)); // press edge
        (void)scene.tick(state);
        CHECK(scene.phase() == DemoScene::Phase::Playing);
        CHECK(scene.score() == 0);
        CHECK(scene.tickCount() == 0);

        // Still holding A: the run must continue, not restart every tick.
        state.update(input::maskOf(Button::A));
        (void)scene.tick(state);
        CHECK(scene.tickCount() == 1);
    }

    TEST_CASE("input during play does not restart anything") {
        DemoScene scene(1);
        InputState state;
        state.update(input::maskOf(Button::A));
        (void)scene.tick(state);
        CHECK(scene.phase() == DemoScene::Phase::Playing);
        CHECK(scene.tickCount() == 1);
    }

    TEST_CASE("rendering does not change state") {
        // Golden tests are only meaningful if drawing is a pure function of state.
        DemoScene scene(77);
        (void)run(scene, heldFor(input::maskOf(Button::Down), 50));
        const std::uint64_t before = scene.stateHash();

        Framebuffer first;
        Framebuffer second;
        scene.render(first);
        scene.render(second);

        CHECK(scene.stateHash() == before);
        CHECK(first == second);
    }

} // TEST_SUITE

TEST_SUITE("golden") {

    TEST_CASE("the opening frame") {
        const DemoScene scene(12345);
        Framebuffer frame;
        scene.render(frame);
        wumpo::testing::checkGolden("demo-opening", frame);
    }

    TEST_CASE("mid-run frame") {
        DemoScene scene(12345);
        InputState state;
        for (int i = 0; i < 300; ++i) {
            state.update(i < 150 ? input::maskOf(Button::Right) : input::maskOf(Button::Down));
            (void)scene.tick(state);
        }
        Framebuffer frame;
        scene.render(frame);
        wumpo::testing::checkGolden("demo-midrun", frame);
    }

    TEST_CASE("the game over screen") {
        DemoScene scene(12345);
        InputState state;
        for (int i = 0; i < DemoScene::kRunTicks; ++i) {
            state.update(0);
            (void)scene.tick(state);
        }
        REQUIRE(scene.phase() == DemoScene::Phase::Over);

        Framebuffer frame;
        scene.render(frame);
        wumpo::testing::checkGolden("demo-gameover", frame);
    }

} // TEST_SUITE
