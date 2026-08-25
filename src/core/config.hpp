#pragma once

#include <cstdint>

/// Virtual hardware profile WUMPO_V1_DEV.
///
/// Every constant describing the device lives here and nowhere else. A literal
/// 64 or 32 anywhere else in the codebase is a bug: it is what makes a change of
/// screen size a two-line edit instead of an archaeology project.
///
/// See docs/architecture/virtual-hardware.md.
namespace wumpo::config {

/// Display.
inline constexpr int kScreenWidth = 64;
inline constexpr int kScreenHeight = 32;

/// One bit per pixel, packed 8 horizontal pixels per byte, MSB leftmost.
inline constexpr int kPixelsPerByte = 8;
inline constexpr int kBytesPerRow = kScreenWidth / kPixelsPerByte;
inline constexpr int kFramebufferBytes = kBytesPerRow * kScreenHeight;

/// Simulation. Rendering is free to run at any rate; this one is fixed.
inline constexpr int kTickHz = 60;

inline constexpr std::int64_t kMicrosecondsPerSecond = 1'000'000;

/// Nominal tick period, for display and diagnostics only.
///
/// Deliberately NOT the unit the loop accumulates in: 1'000'000 / 60 truncates
/// to 16666, losing 40 microseconds every second. The loop instead accumulates
/// in microsecond-ticks (elapsed * kTickHz) and compares against
/// kMicrosecondsPerSecond, which is exact for any tick rate. See src/core/loop.
inline constexpr std::int64_t kNominalMicrosecondsPerTick = kMicrosecondsPerSecond / kTickHz;

/// How many simulation ticks a single frame may catch up on before the loop
/// gives up and drops the remainder. Without a cap, a stalled host produces an
/// ever-growing backlog it can never work through.
inline constexpr int kMaxCatchupTicks = 5;

/// Input. Six physical buttons, one bit each, so a full input state is one byte.
inline constexpr int kButtonCount = 6;

/// Audio: a single square-wave channel, like a piezo buzzer.
inline constexpr int kAudioChannels = 1;
inline constexpr int kToneQueueCapacity = 8;

/// Persistent storage: one fixed block, sized like a small EEPROM page.
inline constexpr int kStorageBytes = 256;

} // namespace wumpo::config
