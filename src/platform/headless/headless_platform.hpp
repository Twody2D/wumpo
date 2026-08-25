#pragma once

#include "core/config.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

/// A complete platform with no host at all: no window, no sound card, no files,
/// no wall clock.
///
/// This is not a stub. It is the second implementation that keeps the hardware
/// boundary honest - an interface with only one implementation is untested by
/// construction - and it is what lets the entire test suite, golden rendering
/// and `--headless` runs work in CI on a machine with no display and no SDL.
///
/// Every part of it is inspectable, so tests can assert what the game drew,
/// played and saved rather than only that it did not crash.
namespace wumpo::platform::headless {

/// Keeps the last frame presented, and counts how many there were.
class MemoryDisplay final : public Display {
public:
    void present(const renderer::Framebuffer& frame) override {
        last_frame_ = frame;
        ++frames_;
    }

    [[nodiscard]] const renderer::Framebuffer& lastFrame() const noexcept { return last_frame_; }
    [[nodiscard]] int frameCount() const noexcept { return frames_; }

private:
    renderer::Framebuffer last_frame_;
    int frames_ = 0;
};

/// Plays back a scripted sequence of held-masks, one per poll.
///
/// Past the end of the script it keeps returning `heldAfterScript` (nothing, by
/// default), so a test that runs longer than its input simply carries on
/// instead of looping, asserting or running off the end.
class ScriptedInput final : public InputSource {
public:
    ScriptedInput() = default;
    explicit ScriptedInput(std::vector<input::ButtonMask> script) : script_(std::move(script)) {}

    [[nodiscard]] input::ButtonMask pollButtons() override {
        if (position_ >= script_.size()) {
            return held_after_script_;
        }
        return script_[position_++];
    }

    void setScript(std::vector<input::ButtonMask> script) {
        script_ = std::move(script);
        position_ = 0;
    }

    /// What every poll returns once the script runs out. Lets a test hold a
    /// button indefinitely without writing out one entry per tick.
    void holdAfterScript(input::ButtonMask mask) noexcept { held_after_script_ = mask; }

    [[nodiscard]] std::size_t consumed() const noexcept { return position_; }
    [[nodiscard]] bool exhausted() const noexcept { return position_ >= script_.size(); }

private:
    std::vector<input::ButtonMask> script_;
    std::size_t position_ = 0;
    input::ButtonMask held_after_script_ = 0;
};

/// Records tones instead of playing them, so tests can assert that the game
/// made a sound at the right moment without a sound card.
class RecordingAudio final : public Audio {
public:
    struct Tone {
        std::uint16_t frequency_hz;
        std::uint16_t duration_ms;

        [[nodiscard]] bool operator==(const Tone&) const noexcept = default;
    };

    void tone(std::uint16_t frequency_hz, std::uint16_t duration_ms) override {
        tones_.push_back(Tone{.frequency_hz = frequency_hz, .duration_ms = duration_ms});
    }

    void stopAll() override { ++stop_calls_; }

    [[nodiscard]] const std::vector<Tone>& tones() const noexcept { return tones_; }
    [[nodiscard]] int stopCalls() const noexcept { return stop_calls_; }
    void clear() noexcept {
        tones_.clear();
        stop_calls_ = 0;
    }

private:
    std::vector<Tone> tones_;
    int stop_calls_ = 0;
};

/// A block of bytes in RAM, behaving like an unwritten EEPROM until first write.
class MemoryStorage final : public Storage {
public:
    [[nodiscard]] bool read(std::span<std::uint8_t> out) override {
        if (!written_ || out.size() > bytes_.size()) {
            return false;
        }
        std::ranges::copy_n(bytes_.begin(), static_cast<std::ptrdiff_t>(out.size()), out.begin());
        return true;
    }

    [[nodiscard]] bool write(std::span<const std::uint8_t> data) override {
        if (data.size() > bytes_.size()) {
            return false;
        }
        std::ranges::fill(bytes_, std::uint8_t{0});
        std::ranges::copy(data, bytes_.begin());
        written_ = true;
        ++writes_;
        return true;
    }

    [[nodiscard]] std::size_t capacity() const override { return bytes_.size(); }

    /// Number of writes so far. Flash and EEPROM wear out, so a game that saves
    /// every tick is a bug worth catching on desktop.
    [[nodiscard]] int writeCount() const noexcept { return writes_; }

    /// Simulates a device that has never been written to.
    void erase() noexcept {
        std::ranges::fill(bytes_, std::uint8_t{0});
        written_ = false;
    }

    /// Direct access, so a test can corrupt a byte and check the save layer
    /// notices.
    [[nodiscard]] std::span<std::uint8_t> raw() noexcept { return bytes_; }

private:
    std::array<std::uint8_t, config::kStorageBytes> bytes_{};
    bool written_ = false;
    int writes_ = 0;
};

/// Time that only moves when a test says so. This is what makes frame-loop
/// tests deterministic instead of dependent on how fast the machine is.
class ManualClock final : public Clock {
public:
    [[nodiscard]] std::int64_t nowMicroseconds() override { return now_; }

    void advance(std::int64_t microseconds) noexcept { now_ += microseconds; }
    void setTime(std::int64_t microseconds) noexcept { now_ = microseconds; }

private:
    std::int64_t now_ = 0;
};

/// The five pieces above, wired together.
class HeadlessPlatform final : public Platform {
public:
    [[nodiscard]] Display& display() override { return display_; }
    [[nodiscard]] InputSource& input() override { return input_; }
    [[nodiscard]] Audio& audio() override { return audio_; }
    [[nodiscard]] Storage& storage() override { return storage_; }
    [[nodiscard]] Clock& clock() override { return clock_; }

    [[nodiscard]] bool pump() override { return !quit_requested_; }

    /// Concrete accessors, for tests that need to inspect or script a backend.
    [[nodiscard]] MemoryDisplay& memoryDisplay() noexcept { return display_; }
    [[nodiscard]] ScriptedInput& scriptedInput() noexcept { return input_; }
    [[nodiscard]] RecordingAudio& recordingAudio() noexcept { return audio_; }
    [[nodiscard]] MemoryStorage& memoryStorage() noexcept { return storage_; }
    [[nodiscard]] ManualClock& manualClock() noexcept { return clock_; }

    void requestQuit() noexcept { quit_requested_ = true; }

private:
    MemoryDisplay display_;
    ScriptedInput input_;
    RecordingAudio audio_;
    MemoryStorage storage_;
    ManualClock clock_;
    bool quit_requested_ = false;
};

} // namespace wumpo::platform::headless
