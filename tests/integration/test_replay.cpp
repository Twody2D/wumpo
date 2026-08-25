#include "core/replay.hpp"
#include "game/demo_scene.hpp"
#include "input/button.hpp"
#include "input/input_state.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>

using wumpo::core::Replay;
using wumpo::game::DemoScene;
using wumpo::input::Button;
using wumpo::input::ButtonMask;
using wumpo::input::InputState;
namespace core = wumpo::core;
namespace input = wumpo::input;

namespace {

std::uint64_t playBack(const Replay& replay) {
    DemoScene scene(replay.seed);
    InputState state;
    for (const ButtonMask mask : replay.inputs) {
        state.update(mask);
        (void)scene.tick(state);
    }
    return scene.stateHash();
}

} // namespace

TEST_SUITE("replay") {

    TEST_CASE("masks round-trip through text") {
        for (unsigned raw = 0; raw <= unsigned{input::kAllButtons}; ++raw) {
            const auto mask = static_cast<ButtonMask>(raw);
            const std::string text = core::maskToText(mask);
            CHECK(core::maskFromText(text) == std::optional<ButtonMask>{mask});
        }
    }

    TEST_CASE("mask text is stable and readable") {
        CHECK(core::maskToText(0).empty());
        CHECK(core::maskToText(input::maskOf(Button::A)) == "A");
        CHECK(core::maskToText(static_cast<ButtonMask>(input::maskOf(Button::Left) |
                                                       input::maskOf(Button::A))) == "LEFT+A");

        // Button order is fixed, so two recordings of the same run are identical
        // files rather than differing by field order.
        CHECK(core::maskToText(static_cast<ButtonMask>(input::maskOf(Button::A) |
                                                       input::maskOf(Button::Left))) == "LEFT+A");
    }

    TEST_CASE("malformed mask text is rejected rather than guessed") {
        CHECK_FALSE(core::maskFromText("START").has_value());
        CHECK_FALSE(core::maskFromText("LEFT+").has_value());
        CHECK_FALSE(core::maskFromText("+A").has_value());
        CHECK_FALSE(core::maskFromText("LEFT++A").has_value());
        CHECK_FALSE(core::maskFromText("left").has_value());
    }

    TEST_CASE("the example from the design document parses") {
        // The format sketch in the master document. It must stay valid: recordings
        // written by hand, and the docs themselves, depend on it.
        const std::string text = R"({
      "version": 1,
      "seed": 12345,
      "inputs": ["LEFT", "LEFT", "A", "RIGHT"]
    })";

        Replay replay;
        std::string error;
        REQUIRE_MESSAGE(core::fromJson(text, replay, &error), error);
        CHECK(replay.version == 1);
        CHECK(replay.seed == 12345);
        REQUIRE(replay.inputs.size() == 4);
        CHECK(replay.inputs[0] == input::maskOf(Button::Left));
        CHECK(replay.inputs[2] == input::maskOf(Button::A));
    }

    TEST_CASE("a replay survives a write and read") {
        Replay original;
        original.seed = 987654321;
        original.inputs = {
            0, input::maskOf(Button::Left),
            static_cast<ButtonMask>(input::maskOf(Button::Left) | input::maskOf(Button::A)), 0,
            input::maskOf(Button::B)};

        const std::string json = core::toJson(original);
        Replay parsed;
        std::string error;
        REQUIRE_MESSAGE(core::fromJson(json, parsed, &error), error);
        CHECK(parsed == original);
    }

    TEST_CASE("an empty replay is valid") {
        Replay empty;
        empty.seed = 5;
        const std::string json = core::toJson(empty);

        Replay parsed;
        std::string error;
        REQUIRE_MESSAGE(core::fromJson(json, parsed, &error), error);
        CHECK(parsed.inputs.empty());
        CHECK(parsed.seed == 5);
    }

    TEST_CASE("replaying a recording reproduces the run exactly") {
        // The whole point of the format. Record by playing, then verify by replaying
        // the file and comparing state hashes.
        Replay recording;
        recording.seed = 4242;

        DemoScene live(recording.seed);
        InputState state;
        for (int tick = 0; tick < 400; ++tick) {
            // A pattern with holds, releases and combinations, so edges matter.
            ButtonMask mask = 0;
            if (tick % 7 < 3) {
                mask = static_cast<ButtonMask>(mask | input::maskOf(Button::Right));
            }
            if (tick % 11 < 4) {
                mask = static_cast<ButtonMask>(mask | input::maskOf(Button::Down));
            }
            if (tick % 23 == 0) {
                mask = static_cast<ButtonMask>(mask | input::maskOf(Button::A));
            }
            recording.inputs.push_back(mask);

            state.update(mask);
            (void)live.tick(state);
        }

        const std::uint64_t live_hash = live.stateHash();

        // Round-tripped through the file format, not just through memory: this
        // catches a serializer that loses a tick as well as a divergent simulation.
        const std::string json = core::toJson(recording);
        Replay parsed;
        std::string error;
        REQUIRE_MESSAGE(core::fromJson(json, parsed, &error), error);

        CHECK(playBack(parsed) == live_hash);
    }

    TEST_CASE("a replay of a different seed does not match") {
        // Guards against a state hash that ignores the parts a seed controls, which
        // would make every replay test pass for the wrong reason.
        Replay recording;
        recording.seed = 1;
        recording.inputs.assign(200, input::maskOf(Button::Right));

        const std::uint64_t first = playBack(recording);
        recording.seed = 2;
        CHECK(playBack(recording) != first);
    }

    TEST_CASE("a truncated file is rejected") {
        Replay replay;
        std::string error;
        CHECK_FALSE(core::fromJson(R"({"version": 1, "seed": 1, "inputs": ["A")", replay, &error));
        CHECK_FALSE(error.empty());

        CHECK_FALSE(core::fromJson(R"({"version": 1)", replay, &error));
        CHECK_FALSE(core::fromJson("{", replay, &error));
        CHECK_FALSE(core::fromJson("", replay, &error));
    }

    TEST_CASE("missing fields are rejected") {
        Replay replay;
        std::string error;
        CHECK_FALSE(core::fromJson(R"({"seed": 1, "inputs": []})", replay, &error));
        CHECK_FALSE(core::fromJson(R"({"version": 1, "inputs": []})", replay, &error));
        CHECK_FALSE(core::fromJson(R"({"version": 1, "seed": 1})", replay, &error));
    }

    TEST_CASE("a future format version is refused rather than misread") {
        Replay replay;
        std::string error;
        CHECK_FALSE(core::fromJson(R"({"version": 2, "seed": 1, "inputs": []})", replay, &error));
        CHECK(error.find("version") != std::string::npos);
    }

    TEST_CASE("an unknown button in a file is refused") {
        // Better to reject the recording than to replay a different run under the
        // same name and call it verified.
        Replay replay;
        std::string error;
        CHECK_FALSE(
            core::fromJson(R"({"version": 1, "seed": 1, "inputs": ["START"]})", replay, &error));
    }

    TEST_CASE("junk is rejected without reading past it") {
        Replay replay;
        std::string error;
        CHECK_FALSE(
            core::fromJson(R"({"version": 1, "seed": 1, "inputs": [1, 2]})", replay, &error));
        CHECK_FALSE(core::fromJson(R"({"version": "x", "seed": 1, "inputs": []})", replay, &error));
        CHECK_FALSE(core::fromJson(R"({"nope": 1})", replay, &error));
        CHECK_FALSE(
            core::fromJson(R"({"version": 1, "seed": 1, "inputs": []} trailing)", replay, &error));
        CHECK_FALSE(
            core::fromJson(R"({"version": 1, "seed": 1, "inputs": ["A\n"]})", replay, &error));
    }

    TEST_CASE("a failed parse leaves the destination untouched") {
        Replay replay;
        replay.seed = 777;
        replay.inputs = {input::maskOf(Button::B)};

        std::string error;
        CHECK_FALSE(core::fromJson("garbage", replay, &error));
        CHECK(replay.seed == 777);
        CHECK(replay.inputs.size() == 1);
    }

} // TEST_SUITE
