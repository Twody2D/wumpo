#include "input/button.hpp"
#include "platform/headless/headless_platform.hpp"
#include "platform/platform.hpp"
#include "renderer/framebuffer.hpp"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <vector>

using wumpo::input::Button;
using wumpo::input::ButtonMask;
using wumpo::platform::Platform;
using wumpo::platform::headless::HeadlessPlatform;
using wumpo::renderer::Framebuffer;
namespace input = wumpo::input;

namespace {

/// Takes the abstract interface on purpose: if this stops compiling, the
/// headless backend has drifted away from the contract hardware will implement.
int countFrames(Platform& platform, int frames) {
    Framebuffer frame;
    for (int i = 0; i < frames; ++i) {
        frame.setPixel(i % Framebuffer::kWidth, 0, true);
        platform.display().present(frame);
    }
    return frames;
}

} // namespace

TEST_SUITE("platform") {

    TEST_CASE("the headless backend satisfies the platform contract") {
        HeadlessPlatform platform;
        Platform& abstract = platform;

        CHECK(countFrames(abstract, 3) == 3);
        CHECK(platform.memoryDisplay().frameCount() == 3);
        CHECK(abstract.pump());

        abstract.audio().tone(440, 100);
        CHECK(platform.recordingAudio().tones().size() == 1);

        CHECK(abstract.clock().nowMicroseconds() == 0);
        CHECK(abstract.storage().capacity() == 256);
    }

    TEST_CASE("the display keeps the exact frame it was given") {
        HeadlessPlatform platform;
        Framebuffer frame;
        frame.setPixel(10, 10, true);
        frame.setPixel(63, 31, true);
        platform.display().present(frame);

        CHECK(platform.memoryDisplay().lastFrame() == frame);
        CHECK(platform.memoryDisplay().lastFrame().pixel(10, 10));
    }

    TEST_CASE("scripted input plays back once and then holds steady") {
        HeadlessPlatform platform;
        platform.scriptedInput().setScript(
            {input::maskOf(Button::Left), input::maskOf(Button::A), 0});

        CHECK(platform.input().pollButtons() == input::maskOf(Button::Left));
        CHECK(platform.input().pollButtons() == input::maskOf(Button::A));
        CHECK(platform.input().pollButtons() == 0);

        // Running past the end is normal, not an error: a test that runs longer
        // than its script just continues with nothing held.
        CHECK(platform.scriptedInput().exhausted());
        CHECK(platform.input().pollButtons() == 0);
        CHECK(platform.input().pollButtons() == 0);
    }

    TEST_CASE("input can be held indefinitely without scripting every tick") {
        HeadlessPlatform platform;
        platform.scriptedInput().holdAfterScript(input::maskOf(Button::Right));
        for (int i = 0; i < 100; ++i) {
            CHECK(platform.input().pollButtons() == input::maskOf(Button::Right));
        }
    }

    TEST_CASE("audio records what was played and when it was silenced") {
        using Tone = wumpo::platform::headless::RecordingAudio::Tone;
        HeadlessPlatform platform;

        platform.audio().tone(880, 50);
        platform.audio().tone(0, 20); // a rest
        platform.audio().stopAll();

        const auto& tones = platform.recordingAudio().tones();
        REQUIRE(tones.size() == 2);
        CHECK(tones[0] == Tone{.frequency_hz = 880, .duration_ms = 50});
        CHECK(tones[1] == Tone{.frequency_hz = 0, .duration_ms = 20});
        CHECK(platform.recordingAudio().stopCalls() == 1);
    }

    TEST_CASE("storage reads back exactly what was written") {
        HeadlessPlatform platform;
        const std::array<std::uint8_t, 4> written{1, 2, 3, 4};
        CHECK(platform.storage().write(written));

        std::array<std::uint8_t, 4> read{};
        CHECK(platform.storage().read(read));
        CHECK(read == written);
    }

    TEST_CASE("an unwritten device reads as empty rather than as garbage") {
        // The state a brand new board is in. A save layer that treats a failed read
        // as "no save yet" instead of trusting uninitialised bytes depends on this.
        HeadlessPlatform platform;
        std::array<std::uint8_t, 4> read{9, 9, 9, 9};
        CHECK_FALSE(platform.storage().read(read));
        // A failed read must not have touched the caller's buffer.
        CHECK(read == std::array<std::uint8_t, 4>{9, 9, 9, 9});
    }

    TEST_CASE("a write larger than the device is refused, not truncated") {
        HeadlessPlatform platform;
        const std::vector<std::uint8_t> too_big(platform.storage().capacity() + 1, 0xAB);
        CHECK_FALSE(platform.storage().write(too_big));
        CHECK(platform.memoryStorage().writeCount() == 0);
    }

    TEST_CASE("a write replaces the previous contents entirely") {
        HeadlessPlatform platform;
        const std::array<std::uint8_t, 4> first{1, 2, 3, 4};
        const std::array<std::uint8_t, 2> second{9, 9};
        CHECK(platform.storage().write(first));
        CHECK(platform.storage().write(second));

        std::array<std::uint8_t, 4> read{};
        CHECK(platform.storage().read(read));
        // Leftovers from the longer first write must not survive underneath.
        CHECK(read == std::array<std::uint8_t, 4>{9, 9, 0, 0});
        CHECK(platform.memoryStorage().writeCount() == 2);
    }

    TEST_CASE("the clock only moves when told to") {
        HeadlessPlatform platform;
        CHECK(platform.clock().nowMicroseconds() == 0);
        platform.manualClock().advance(16'666);
        CHECK(platform.clock().nowMicroseconds() == 16'666);
        platform.manualClock().advance(16'666);
        CHECK(platform.clock().nowMicroseconds() == 33'332);
    }

    TEST_CASE("pump reports a quit request") {
        HeadlessPlatform platform;
        CHECK(platform.pump());
        platform.requestQuit();
        CHECK_FALSE(platform.pump());
    }

} // TEST_SUITE
