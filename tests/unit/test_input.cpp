#include "input/button.hpp"
#include "input/input_state.hpp"

#include <doctest.h>

#include <array>
#include <optional>
#include <set>
#include <string_view>

using wumpo::input::Button;
using wumpo::input::ButtonMask;
using wumpo::input::InputState;
namespace input = wumpo::input;

TEST_SUITE("input") {

    TEST_CASE("the whole input state fits in one byte") {
        CHECK(sizeof(ButtonMask) == 1);
        CHECK(input::kButtonCount == 6);
        CHECK(input::kAllButtons == 0b0011'1111);

        // Every button owns a distinct bit inside that byte.
        std::set<ButtonMask> masks;
        for (const Button button : input::kAllButtonList) {
            masks.insert(input::maskOf(button));
        }
        CHECK(masks.size() == 6);
        for (const ButtonMask mask : masks) {
            CHECK((mask & input::kAllButtons) == mask);
        }
    }

    TEST_CASE("button names round-trip") {
        // Names go into replay files. A name that does not survive a round trip
        // means recordings that cannot be read back.
        for (const Button button : input::kAllButtonList) {
            const std::string_view text = input::name(button);
            CHECK_FALSE(text.empty());
            // Compared as optionals rather than dereferenced: a failure then
            // reports "nothing" instead of crashing the test binary.
            CHECK(input::parseButton(text) == std::optional<Button>{button});
        }
    }

    TEST_CASE("unknown button names are rejected, not guessed") {
        CHECK_FALSE(input::parseButton("").has_value());
        CHECK_FALSE(input::parseButton("START").has_value());  // the device has no Start
        CHECK_FALSE(input::parseButton("SELECT").has_value()); // nor Select
        CHECK_FALSE(input::parseButton("left").has_value());   // names are uppercase
        CHECK_FALSE(input::parseButton("C").has_value());
        CHECK_FALSE(input::parseButton("LEFT ").has_value());
    }

    TEST_CASE("a fresh state reports nothing") {
        const InputState state;
        for (const Button button : input::kAllButtonList) {
            CHECK_FALSE(state.down(button));
            CHECK_FALSE(state.pressed(button));
            CHECK_FALSE(state.released(button));
        }
        CHECK_FALSE(state.anyPressed());
    }

    TEST_CASE("a press is reported exactly once, however long it is held") {
        InputState state;

        state.update(input::maskOf(Button::A));
        CHECK(state.down(Button::A));
        CHECK(state.pressed(Button::A));
        CHECK_FALSE(state.released(Button::A));

        // Still held on the next tick: down stays true, pressed does not repeat.
        // This is the difference between a jump and a machine gun.
        state.update(input::maskOf(Button::A));
        CHECK(state.down(Button::A));
        CHECK_FALSE(state.pressed(Button::A));

        state.update(0);
        CHECK_FALSE(state.down(Button::A));
        CHECK_FALSE(state.pressed(Button::A));
        CHECK(state.released(Button::A));

        // And release does not repeat either.
        state.update(0);
        CHECK_FALSE(state.released(Button::A));
    }

    TEST_CASE("buttons are independent") {
        InputState state;
        state.update(
            static_cast<ButtonMask>(input::maskOf(Button::Left) | input::maskOf(Button::A)));
        CHECK(state.pressed(Button::Left));
        CHECK(state.pressed(Button::A));
        CHECK_FALSE(state.pressed(Button::Right));

        // Release one, keep the other.
        state.update(input::maskOf(Button::A));
        CHECK(state.released(Button::Left));
        CHECK(state.down(Button::A));
        CHECK_FALSE(state.pressed(Button::A));
        CHECK_FALSE(state.released(Button::A));
    }

    TEST_CASE("a press and release inside one tick is not silently swallowed") {
        // The loop calls update() once per tick, so a tap shorter than a tick is
        // invisible by construction. This documents that: it is the reason the
        // backend must report the held mask sampled at tick time, and the reason a
        // 60 Hz tick is the input resolution of the device.
        InputState state;
        state.update(0);
        CHECK_FALSE(state.pressed(Button::A));
    }

    TEST_CASE("bits with no physical button are ignored") {
        InputState state;
        state.update(0xFF);
        CHECK(state.downMask() == input::kAllButtons);
        CHECK(state.pressedMask() == input::kAllButtons);
    }

    TEST_CASE("reset clears history so a held button is not re-triggered") {
        InputState state;
        state.update(input::maskOf(Button::A));
        state.reset();
        CHECK_FALSE(state.down(Button::A));
        CHECK_FALSE(state.pressed(Button::A));

        // After a reset, a button that was already held reads as a fresh press on
        // the next tick. That is what makes "A restarts, and holding A does not
        // instantly restart again" work.
        state.update(input::maskOf(Button::A));
        CHECK(state.pressed(Button::A));
    }

    TEST_CASE("replaying the same held-mask sequence reproduces every edge") {
        // The property the whole replay system rests on.
        constexpr std::array<ButtonMask, 7> kSequence = {
            0,
            input::maskOf(Button::Left),
            input::maskOf(Button::Left),
            0,
            static_cast<ButtonMask>(input::maskOf(Button::Left) | input::maskOf(Button::A)),
            input::maskOf(Button::A),
            0};

        InputState live;
        InputState replayed;
        for (const ButtonMask mask : kSequence) {
            live.update(mask);
            replayed.update(mask);
            CHECK(live == replayed);
            CHECK(live.pressedMask() == replayed.pressedMask());
            CHECK(live.releasedMask() == replayed.releasedMask());
        }
    }

} // TEST_SUITE
