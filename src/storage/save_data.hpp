#pragma once

#include "core/config.hpp"
#include "platform/platform.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace wumpo::storage {

/// Everything the device remembers between power cycles.
///
/// Deliberately tiny and fixed-size. This is a few hundred bytes of EEPROM or
/// flash, not a database, and every field here costs write endurance on real
/// hardware.
struct SaveData {
    std::uint32_t high_score = 0;
    /// Bit flags: sound on/off and whatever settings appear later. One byte,
    /// because a settings struct that grows without thought is how a 256-byte
    /// budget disappears.
    std::uint8_t settings = kDefaultSettings;

    static constexpr std::uint8_t kSoundEnabled = 1U << 0U;
    static constexpr std::uint8_t kDefaultSettings = kSoundEnabled;

    [[nodiscard]] constexpr bool soundEnabled() const noexcept {
        return (settings & kSoundEnabled) != 0;
    }

    [[nodiscard]] bool operator==(const SaveData&) const noexcept = default;
};

/// Why a load did not produce data. Distinguishing these matters: an empty
/// device is normal and silent, a corrupt one is worth telling the player about
/// once, and a future version is a reason not to overwrite blindly.
enum class LoadResult : std::uint8_t {
    Loaded,
    Empty,        ///< nothing has ever been written here
    BadMagic,     ///< the bytes are not a Wumpo save at all
    BadChecksum,  ///< a save, but damaged
    FutureVersion ///< written by a newer build than this one
};

/// On-device layout: magic, version, payload, checksum, zero padding.
///
/// Versioned from the first byte because the alternative - discovering later
/// that saves need a version - means everyone's high score is lost once.
/// Checksummed because flash wears out and a half-finished write during a
/// battery pull is a normal event, not an exotic one.
namespace layout {
inline constexpr std::array<std::uint8_t, 4> kMagic = {'W', 'U', 'M', 'P'};
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::size_t kBlockSize = config::kStorageBytes;
} // namespace layout

/// Serializes into exactly `layout::kBlockSize` bytes. Never fails.
void serialize(const SaveData& data, std::span<std::uint8_t> out);

/// Parses a block. `data` is left untouched unless the result is `Loaded`.
[[nodiscard]] LoadResult deserialize(std::span<const std::uint8_t> block, SaveData& data);

/// Reads through a platform Storage. An unwritten device reports `Empty`, which
/// is not an error: it is what a new console looks like.
[[nodiscard]] LoadResult load(platform::Storage& storage, SaveData& data);

/// Writes through a platform Storage.
[[nodiscard]] bool store(platform::Storage& storage, const SaveData& data);

/// CRC-16/CCITT-FALSE. Exposed for tests; small enough to run on any MCU
/// without a table.
[[nodiscard]] std::uint16_t checksum(std::span<const std::uint8_t> bytes) noexcept;

} // namespace wumpo::storage
