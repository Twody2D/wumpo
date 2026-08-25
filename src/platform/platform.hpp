#pragma once

#include "input/button.hpp"
#include "renderer/framebuffer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

/// The hardware boundary. Everything above it - game, runtime, renderer - is
/// portable; everything below it is a backend.
///
/// There are exactly five interfaces, one per thing the device physically has.
/// Nothing else gets an interface: a type with one implementation and no second
/// one coming is a class, not an abstraction.
///
/// See docs/decisions/ADR-004-platform-abstraction.md.
namespace wumpo::platform {

/// Shows a finished frame. Whatever a backend must do to get 2048 pixels onto
/// glass - scale them, transpose them for a display controller, push them over
/// a bus - happens here and is invisible above.
class Display {
public:
    Display() = default;
    Display(const Display&) = delete;
    Display(Display&&) = delete;
    Display& operator=(const Display&) = delete;
    Display& operator=(Display&&) = delete;
    virtual ~Display() = default;

    virtual void present(const renderer::Framebuffer& frame) = 0;
};

/// Reports which buttons are held, right now, as a bitmask.
///
/// Held state only, deliberately: edges are derived in the runtime so that a
/// replay of raw masks reproduces them exactly. A backend that reported presses
/// itself would make replays depend on its frame timing.
class InputSource {
public:
    InputSource() = default;
    InputSource(const InputSource&) = delete;
    InputSource(InputSource&&) = delete;
    InputSource& operator=(const InputSource&) = delete;
    InputSource& operator=(InputSource&&) = delete;
    virtual ~InputSource() = default;

    [[nodiscard]] virtual input::ButtonMask pollButtons() = 0;
};

/// One square-wave channel, like a piezo buzzer.
///
/// Strictly an output: the simulation calls it and never reads back, so audio
/// cannot influence game state and cannot break determinism.
class Audio {
public:
    Audio() = default;
    Audio(const Audio&) = delete;
    Audio(Audio&&) = delete;
    Audio& operator=(const Audio&) = delete;
    Audio& operator=(Audio&&) = delete;
    virtual ~Audio() = default;

    /// Queues a tone. A frequency of zero is a rest of the same length.
    virtual void tone(std::uint16_t frequency_hz, std::uint16_t duration_ms) = 0;

    /// Drops everything queued and silences the channel immediately.
    virtual void stopAll() = 0;
};

/// One fixed block of persistent bytes.
///
/// Raw bytes rather than typed values, so the eventual EEPROM or flash
/// implementation stays trivial. Versioning and checksums live one layer up, in
/// src/storage, where they can be tested without hardware.
class Storage {
public:
    Storage() = default;
    Storage(const Storage&) = delete;
    Storage(Storage&&) = delete;
    Storage& operator=(const Storage&) = delete;
    Storage& operator=(Storage&&) = delete;
    virtual ~Storage() = default;

    /// Fills `out` with the stored block. Returns false if nothing is stored yet
    /// or the read failed; `out` is then left untouched.
    [[nodiscard]] virtual bool read(std::span<std::uint8_t> out) = 0;

    [[nodiscard]] virtual bool write(std::span<const std::uint8_t> data) = 0;

    [[nodiscard]] virtual std::size_t capacity() const = 0;
};

/// Monotonic time. Used only by the frame loop to decide how many ticks to run;
/// the simulation itself never sees it.
class Clock {
public:
    Clock() = default;
    Clock(const Clock&) = delete;
    Clock(Clock&&) = delete;
    Clock& operator=(const Clock&) = delete;
    Clock& operator=(Clock&&) = delete;
    virtual ~Clock() = default;

    /// Microseconds since an unspecified origin. Must never go backwards.
    [[nodiscard]] virtual std::int64_t nowMicroseconds() = 0;
};

/// Bundles the five interfaces plus the one thing a host needs that a device
/// does not: a chance to process window events and ask the program to stop.
class Platform {
public:
    Platform() = default;
    Platform(const Platform&) = delete;
    Platform(Platform&&) = delete;
    Platform& operator=(const Platform&) = delete;
    Platform& operator=(Platform&&) = delete;
    virtual ~Platform() = default;

    [[nodiscard]] virtual Display& display() = 0;
    [[nodiscard]] virtual InputSource& input() = 0;
    [[nodiscard]] virtual Audio& audio() = 0;
    [[nodiscard]] virtual Storage& storage() = 0;
    [[nodiscard]] virtual Clock& clock() = 0;

    /// Processes host events. Returns false when the host asks the program to
    /// quit - a closed window, Ctrl+C. On hardware this always returns true:
    /// there is nowhere to quit to.
    [[nodiscard]] virtual bool pump() = 0;
};

} // namespace wumpo::platform
