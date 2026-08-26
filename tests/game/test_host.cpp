#include "game/host.hpp"
#include "input/button.hpp"
#include "input/input_state.hpp"
#include "renderer/framebuffer.hpp"
#include "storage/save_data.hpp"

#include <doctest.h>

#include <cstdint>
#include <vector>

using wumpo::game::GameHost;
using wumpo::input::Button;
using wumpo::input::ButtonMask;
using wumpo::input::InputState;
using wumpo::renderer::Framebuffer;
using wumpo::storage::GameId;
using wumpo::storage::SaveData;
namespace input = wumpo::input;

namespace {

std::vector<ButtonMask> heldFor(ButtonMask mask, int ticks) {
    return std::vector<ButtonMask>(static_cast<std::size_t>(ticks), mask);
}

void run(GameHost& host, InputState& state, const std::vector<ButtonMask>& masks) {
    for (const ButtonMask mask : masks) {
        state.update(mask);
        (void)host.tick(state);
    }
}

/// Holds A and B together long enough to open the switcher. Leaves `state` mid
/// tick sequence so a caller can immediately act on the now-open selector.
void openSwitcher(GameHost& host, InputState& state) {
    const auto chord = static_cast<ButtonMask>(input::maskOf(Button::A) | input::maskOf(Button::B));
    for (int i = 0; i < GameHost::kSwitchHoldTicks; ++i) {
        state.update(chord);
        (void)host.tick(state);
    }
}

} // namespace

TEST_SUITE("game") {

    TEST_CASE("a fresh save boots into Shift") {
        const GameHost host(1, SaveData{});
        CHECK(host.activeGame() == GameId::Shift);
        CHECK(host.mode() == GameHost::Mode::Playing);
        CHECK(host.tickCount() == 0);
    }

    TEST_CASE("last_played decides which game boots, and its saved high score carries over") {
        SaveData save;
        save.last_played = GameId::Echo;
        save.setHighScore(GameId::Echo, 42);

        const GameHost host(1, save);
        CHECK(host.activeGame() == GameId::Echo);
        CHECK(host.highScore(GameId::Echo) == 42);
    }

    TEST_CASE("input reaches the active game while playing") {
        GameHost host(1, SaveData{});
        InputState state;
        run(host, state, heldFor(input::maskOf(Button::Right), 50));
        CHECK(host.tickCount() == 50);
    }

    TEST_CASE("holding A and B together opens the switcher only once the hold is long enough") {
        GameHost host(1, SaveData{});
        InputState state;
        const auto chord =
            static_cast<ButtonMask>(input::maskOf(Button::A) | input::maskOf(Button::B));

        for (int i = 0; i < GameHost::kSwitchHoldTicks - 1; ++i) {
            state.update(chord);
            (void)host.tick(state);
        }
        CHECK(host.mode() == GameHost::Mode::Playing);

        state.update(chord);
        (void)host.tick(state);
        CHECK(host.mode() == GameHost::Mode::Selecting);
    }

    TEST_CASE("input does not reach the game while the switcher is open") {
        GameHost host(1, SaveData{});
        InputState state;
        openSwitcher(host, state);
        REQUIRE(host.mode() == GameHost::Mode::Selecting);
        const int ticks_before = host.tickCount();

        run(host, state, heldFor(input::maskOf(Button::Right), 30));
        CHECK(host.mode() == GameHost::Mode::Selecting); // Right is not a selector command
        CHECK(host.tickCount() == ticks_before);
    }

    TEST_CASE("Down moves the cursor and A switches to the selected game, starting it fresh") {
        GameHost host(1, SaveData{});
        InputState state;
        run(host, state, heldFor(input::maskOf(Button::Right), 20)); // rack up some progress
        openSwitcher(host, state);
        REQUIRE(host.mode() == GameHost::Mode::Selecting);

        state.update(input::maskOf(Button::Down)); // press edge, Shift -> Echo
        (void)host.tick(state);
        state.update(0);
        (void)host.tick(state);
        state.update(input::maskOf(Button::A));
        (void)host.tick(state);

        CHECK(host.mode() == GameHost::Mode::Playing);
        CHECK(host.activeGame() == GameId::Echo);
        CHECK(host.tickCount() == 0); // a fresh instance, not the old Shift run
    }

    TEST_CASE("B cancels the switcher without changing the active game") {
        GameHost host(1, SaveData{});
        InputState state;
        openSwitcher(host, state);
        REQUIRE(host.mode() == GameHost::Mode::Selecting);

        state.update(input::maskOf(Button::Down));
        (void)host.tick(state);
        state.update(0);
        (void)host.tick(state);
        state.update(input::maskOf(Button::B));
        (void)host.tick(state);

        CHECK(host.mode() == GameHost::Mode::Playing);
        CHECK(host.activeGame() == GameId::Shift);
    }

    TEST_CASE("switching away and back preserves each game's own high score") {
        SaveData save;
        save.setHighScore(GameId::Shift, 20);
        GameHost host(1, save);
        InputState state;

        openSwitcher(host, state);
        state.update(input::maskOf(Button::Down));
        (void)host.tick(state);
        state.update(0);
        (void)host.tick(state);
        state.update(input::maskOf(Button::A));
        (void)host.tick(state);
        REQUIRE(host.activeGame() == GameId::Echo);
        CHECK(host.highScore(GameId::Shift) == 20); // untouched by switching away

        openSwitcher(host, state);
        state.update(input::maskOf(Button::Up));
        (void)host.tick(state);
        state.update(0);
        (void)host.tick(state);
        state.update(input::maskOf(Button::A));
        (void)host.tick(state);
        REQUIRE(host.activeGame() == GameId::Shift);
        CHECK(host.highScore(GameId::Shift) == 20);
    }

    TEST_CASE("the same seed, save and inputs produce the same final state") {
        const auto script = heldFor(input::maskOf(Button::Right), 200);
        SaveData save;

        GameHost first(4242, save);
        InputState first_state;
        run(first, first_state, script);

        GameHost second(4242, save);
        InputState second_state;
        run(second, second_state, script);

        CHECK(first.stateHash() == second.stateHash());
    }

    TEST_CASE("different inputs diverge from the same seed and save") {
        SaveData save;
        GameHost left(9, save);
        InputState left_state;
        run(left, left_state, heldFor(input::maskOf(Button::Left), 60));

        GameHost right(9, save);
        InputState right_state;
        run(right, right_state, heldFor(input::maskOf(Button::Right), 60));

        CHECK(left.stateHash() != right.stateHash());
    }

    TEST_CASE("rendering does not change state") {
        GameHost host(77, SaveData{});
        InputState state;
        run(host, state, heldFor(input::maskOf(Button::Right), 50));
        const std::uint64_t before = host.stateHash();

        Framebuffer first;
        Framebuffer second;
        host.render(first);
        host.render(second);

        CHECK(host.stateHash() == before);
        CHECK(first == second);
    }

    TEST_CASE("the selector screen also renders without changing state") {
        GameHost host(1, SaveData{});
        InputState state;
        openSwitcher(host, state);
        REQUIRE(host.mode() == GameHost::Mode::Selecting);
        const std::uint64_t before = host.stateHash();

        Framebuffer frame;
        host.render(frame);
        CHECK(host.stateHash() == before);
    }

} // TEST_SUITE
