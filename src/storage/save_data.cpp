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
constexpr std::size_t kHighScoreOffset = 5; // 4 bytes, little endian
constexpr std::size_t kSettingsOffset = 9;  // 1 byte
constexpr std::size_t kPayloadEnd = 10;
constexpr std::size_t kChecksumOffset = kPayloadEnd; // 2 bytes
constexpr std::size_t kUsedBytes = kChecksumOffset + 2;

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
    writeUint32(out, kHighScoreOffset, data.high_score);
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

    const std::uint16_t stored_crc =
        static_cast<std::uint16_t>(block[kChecksumOffset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(block[kChecksumOffset + 1]) << 8U);
    if (stored_crc != checksum(block.subspan(0, kPayloadEnd))) {
        return LoadResult::BadChecksum;
    }

    // Checked after the checksum: a corrupt byte could otherwise masquerade as a
    // version from the future and stop the player's real save being repaired.
    if (block[kVersionOffset] > layout::kVersion) {
        return LoadResult::FutureVersion;
    }

    data.high_score = readUint32(block, kHighScoreOffset);
    data.settings = block[kSettingsOffset];
    return LoadResult::Loaded;
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
