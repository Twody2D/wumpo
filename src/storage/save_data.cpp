#include "storage/save_data.hpp"

#include <algorithm>

namespace wumpo::storage {
namespace {

/// Byte offsets inside the block. Written out rather than derived from a struct
/// layout: what goes on the device must not depend on the compiler's padding
/// choices, or a save written by the desktop build would be unreadable by the
/// firmware build.
constexpr std::size_t kMagicOffset = 0;
constexpr std::size_t kVersionOffset = 4;

// Version 2 (current) layout.
constexpr std::size_t kLastPlayedOffset = 5;
constexpr std::size_t kHighScoresOffset = 6; // kMaxGames * 4 bytes, little endian
constexpr std::size_t kSettingsOffset = kHighScoresOffset + (kMaxGames * 4); // 1 byte
constexpr std::size_t kPayloadEnd = kSettingsOffset + 1;
constexpr std::size_t kChecksumOffset = kPayloadEnd; // 2 bytes
constexpr std::size_t kUsedBytes = kChecksumOffset + 2;

// Version 1 (retired) layout, kept only so `deserialize()` can migrate an
// old save instead of discarding it - the reason the format was versioned
// from the first byte in the first place.
constexpr std::size_t kV1HighScoreOffset = 5; // 4 bytes, little endian
constexpr std::size_t kV1SettingsOffset = 9;  // 1 byte
constexpr std::size_t kV1PayloadEnd = 10;
constexpr std::size_t kV1ChecksumOffset = kV1PayloadEnd; // 2 bytes

static_assert(kUsedBytes <= layout::kBlockSize, "save layout does not fit the storage block");

void writeUint32(std::span<std::uint8_t> out, std::size_t offset, std::uint32_t value) {
    out[offset + 0] = static_cast<std::uint8_t>(value & 0xFFU);
    out[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    out[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    out[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

[[nodiscard]] std::uint32_t readUint32(std::span<const std::uint8_t> in, std::size_t offset) {
    return static_cast<std::uint32_t>(in[offset + 0]) |
           (static_cast<std::uint32_t>(in[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(in[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(in[offset + 3]) << 24U);
}

[[nodiscard]] std::uint16_t readUint16(std::span<const std::uint8_t> in, std::size_t offset) {
    return static_cast<std::uint16_t>(in[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[offset + 1]) << 8U);
}

} // namespace

std::uint16_t checksum(std::span<const std::uint8_t> bytes) noexcept {
    std::uint16_t crc = 0xFFFF;
    for (const std::uint8_t byte : bytes) {
        crc ^= static_cast<std::uint16_t>(static_cast<std::uint16_t>(byte) << 8U);
        for (int bit = 0; bit < 8; ++bit) {
            const bool top_set = (crc & 0x8000U) != 0;
            crc = static_cast<std::uint16_t>(crc << 1U);
            if (top_set) {
                crc ^= 0x1021U;
            }
        }
    }
    return crc;
}

void serialize(const SaveData& data, std::span<std::uint8_t> out) {
    std::ranges::fill(out, std::uint8_t{0});

    std::ranges::copy(layout::kMagic, out.begin() + kMagicOffset);
    out[kVersionOffset] = layout::kVersion;
    out[kLastPlayedOffset] = static_cast<std::uint8_t>(data.last_played);
    for (std::size_t i = 0; i < kMaxGames; ++i) {
        writeUint32(out, kHighScoresOffset + (i * 4), data.high_scores[i]);
    }
    out[kSettingsOffset] = data.settings;

    const std::uint16_t crc = checksum(out.subspan(0, kPayloadEnd));
    out[kChecksumOffset] = static_cast<std::uint8_t>(crc & 0xFFU);
    out[kChecksumOffset + 1] = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);
}

LoadResult deserialize(std::span<const std::uint8_t> block, SaveData& data) {
    if (block.size() < kUsedBytes) {
        return LoadResult::Empty;
    }

    // An erased EEPROM reads as all zeroes (or all ones, on some parts). Either
    // way it is not a save, and saying so is different from saying "corrupt".
    const bool all_zero = std::ranges::all_of(block, [](std::uint8_t b) { return b == 0; });
    const bool all_ones = std::ranges::all_of(block, [](std::uint8_t b) { return b == 0xFF; });
    if (all_zero || all_ones) {
        return LoadResult::Empty;
    }

    if (!std::ranges::equal(block.subspan(kMagicOffset, layout::kMagic.size()), layout::kMagic)) {
        return LoadResult::BadMagic;
    }

    // The version decides which layout - and therefore which byte range - the
    // checksum was computed over, so it must be read before the checksum can
    // be checked at all.
    const std::uint8_t version = block[kVersionOffset];

    if (version == 1) {
        const std::uint16_t stored_crc = readUint16(block, kV1ChecksumOffset);
        if (stored_crc != checksum(block.subspan(0, kV1PayloadEnd))) {
            return LoadResult::BadChecksum;
        }
        // Migrate: the one score version 1 knew about becomes Shift's score,
        // since Shift was the only game version 1 could have been written by.
        // This is the payoff of versioning the format from the first byte -
        // an old save is upgraded, not discarded.
        data.high_scores.fill(0);
        data.high_scores[static_cast<std::size_t>(GameId::Shift)] =
            readUint32(block, kV1HighScoreOffset);
        data.last_played = GameId::Shift;
        data.settings = block[kV1SettingsOffset];
        return LoadResult::Loaded;
    }

    if (version == layout::kVersion) {
        const std::uint16_t stored_crc = readUint16(block, kChecksumOffset);
        if (stored_crc != checksum(block.subspan(0, kPayloadEnd))) {
            return LoadResult::BadChecksum;
        }
        // Checked after the checksum: a corrupt byte could otherwise
        // masquerade as a game id we do not have.
        data.last_played = static_cast<GameId>(block[kLastPlayedOffset]);
        for (std::size_t i = 0; i < kMaxGames; ++i) {
            data.high_scores[i] = readUint32(block, kHighScoresOffset + (i * 4));
        }
        data.settings = block[kSettingsOffset];
        return LoadResult::Loaded;
    }

    // Neither a version we ever wrote nor the current one: a newer build's
    // layout we cannot know how to checksum, let alone parse.
    return LoadResult::FutureVersion;
}

LoadResult load(platform::Storage& storage, SaveData& data) {
    std::array<std::uint8_t, layout::kBlockSize> block{};
    if (!storage.read(block)) {
        return LoadResult::Empty;
    }
    return deserialize(block, data);
}

bool store(platform::Storage& storage, const SaveData& data) {
    std::array<std::uint8_t, layout::kBlockSize> block{};
    serialize(data, block);
    return storage.write(block);
}

} // namespace wumpo::storage
