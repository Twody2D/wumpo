#include "platform/headless/headless_platform.hpp"
#include "storage/save_data.hpp"

#include <doctest.h>

#include <array>
#include <cstdint>

using wumpo::platform::headless::HeadlessPlatform;
using wumpo::storage::GameId;
using wumpo::storage::LoadResult;
using wumpo::storage::SaveData;
namespace storage = wumpo::storage;

namespace {

/// Builds a version 1 block by hand, in the layout that shipped before
/// multiple games existed: magic, version, one 4-byte high score, one
/// settings byte, a checksum over just those 10 bytes, zero padding. This is
/// what an old save actually looks like on disk - `deserialize()` must still
/// read it, not just the current version.
std::array<std::uint8_t, storage::layout::kBlockSize> buildV1Block(std::uint32_t high_score,
                                                                   std::uint8_t settings) {
    std::array<std::uint8_t, storage::layout::kBlockSize> block{};
    block[0] = 'W';
    block[1] = 'U';
    block[2] = 'M';
    block[3] = 'P';
    block[4] = 1; // version
    block[5] = static_cast<std::uint8_t>(high_score & 0xFFU);
    block[6] = static_cast<std::uint8_t>((high_score >> 8U) & 0xFFU);
    block[7] = static_cast<std::uint8_t>((high_score >> 16U) & 0xFFU);
    block[8] = static_cast<std::uint8_t>((high_score >> 24U) & 0xFFU);
    block[9] = settings;

    const std::uint16_t crc =
        storage::checksum(std::span<const std::uint8_t>(block).subspan(0, 10));
    block[10] = static_cast<std::uint8_t>(crc & 0xFFU);
    block[11] = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);
    return block;
}

} // namespace

TEST_SUITE("storage") {

    TEST_CASE("a save survives a round trip") {
        HeadlessPlatform platform;
        SaveData written;
        written.setHighScore(GameId::Shift, 1234);
        written.settings = 0;

        CHECK(storage::store(platform.storage(), written));

        SaveData read;
        CHECK(storage::load(platform.storage(), read) == LoadResult::Loaded);
        CHECK(read == written);
    }

    TEST_CASE("high scores round-trip per game, independently") {
        HeadlessPlatform platform;
        SaveData written;
        written.setHighScore(GameId::Shift, 20);
        written.setHighScore(GameId::Echo, 7);
        written.last_played = GameId::Echo;
        CHECK(storage::store(platform.storage(), written));

        SaveData read;
        CHECK(storage::load(platform.storage(), read) == LoadResult::Loaded);
        CHECK(read.highScore(GameId::Shift) == 20);
        CHECK(read.highScore(GameId::Echo) == 7);
        CHECK(read.last_played == GameId::Echo);
    }

    TEST_CASE("a version 1 save is migrated, not discarded") {
        // The whole reason the format was versioned from the first byte:
        // reading an old save must not lose the player's progress.
        const auto block = buildV1Block(500, 1);

        SaveData data;
        REQUIRE(storage::deserialize(block, data) == LoadResult::Loaded);
        CHECK(data.highScore(GameId::Shift) == 500);
        CHECK(data.highScore(GameId::Echo) == 0);
        CHECK(data.last_played == GameId::Shift);
        CHECK(data.settings == 1);
    }

    TEST_CASE("a version 1 save with a damaged checksum is still rejected") {
        auto block = buildV1Block(500, 1);
        block[5] = static_cast<std::uint8_t>(block[5] ^ 0x01);

        SaveData data;
        CHECK(storage::deserialize(block, data) == LoadResult::BadChecksum);
    }

    TEST_CASE("a device that has never been written reports empty, not corrupt") {
        // What a brand new console looks like. Treating this as corruption would
        // show an error to every first-time player.
        HeadlessPlatform platform;
        SaveData data;
        data.setHighScore(GameId::Shift, 99);
        CHECK(storage::load(platform.storage(), data) == LoadResult::Empty);
        // A failed load must not have touched the caller's data.
        CHECK(data.highScore(GameId::Shift) == 99);
    }

    TEST_CASE("an erased device reads as empty whichever way it erases") {
        // Flash erases to 0xFF, EEPROM often reads back 0x00. Neither is a save.
        std::array<std::uint8_t, storage::layout::kBlockSize> block{};
        SaveData data;

        block.fill(0x00);
        CHECK(storage::deserialize(block, data) == LoadResult::Empty);

        block.fill(0xFF);
        CHECK(storage::deserialize(block, data) == LoadResult::Empty);
    }

    TEST_CASE("foreign data is rejected by its magic") {
        std::array<std::uint8_t, storage::layout::kBlockSize> block{};
        block.fill(0x00);
        block[0] = 'N';
        block[1] = 'O';
        block[2] = 'P';
        block[3] = 'E';
        block[9] = 1; // make sure it is not all-zero

        SaveData data;
        CHECK(storage::deserialize(block, data) == LoadResult::BadMagic);
    }

    TEST_CASE("a single flipped bit is caught by the checksum") {
        // The realistic failure: a battery pulled mid-write, or flash decay.
        std::array<std::uint8_t, storage::layout::kBlockSize> block{};
        SaveData written;
        written.setHighScore(GameId::Shift, 4242);
        written.settings = 1;
        storage::serialize(written, block);

        SaveData data;
        REQUIRE(storage::deserialize(block, data) == LoadResult::Loaded);

        block[6] = static_cast<std::uint8_t>(block[6] ^ 0x01);
        CHECK(storage::deserialize(block, data) == LoadResult::BadChecksum);
    }

    TEST_CASE("corrupting the checksum itself is also caught") {
        std::array<std::uint8_t, storage::layout::kBlockSize> block{};
        SaveData written;
        written.setHighScore(GameId::Shift, 7);
        written.settings = 1;
        storage::serialize(written, block);

        const std::size_t checksum_offset = 71;
        block[checksum_offset] = static_cast<std::uint8_t>(block[checksum_offset] ^ 0xFF);

        SaveData data;
        CHECK(storage::deserialize(block, data) == LoadResult::BadChecksum);
    }

    TEST_CASE("a save from a future version is reported, not misread") {
        // Someone runs an older build after a newer one. Reading the newer layout as
        // if it were this one would silently corrupt their progress.
        std::array<std::uint8_t, storage::layout::kBlockSize> block{};
        SaveData written;
        written.setHighScore(GameId::Shift, 500);
        written.settings = 1;
        storage::serialize(written, block);

        const std::size_t version_offset = 4;
        const std::size_t payload_end = 71;
        const std::size_t checksum_offset = payload_end;
        block[version_offset] = storage::layout::kVersion + 1;
        // Recompute the checksum so the block is valid, just newer.
        const std::uint16_t crc =
            storage::checksum(std::span<const std::uint8_t>(block).subspan(0, payload_end));
        block[checksum_offset] = static_cast<std::uint8_t>(crc & 0xFFU);
        block[checksum_offset + 1] = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);

        SaveData data;
        data.setHighScore(GameId::Shift, 1);
        CHECK(storage::deserialize(block, data) == LoadResult::FutureVersion);
        CHECK(data.highScore(GameId::Shift) == 1); // untouched
    }

    TEST_CASE("the serialized block is exactly the storage size") {
        std::array<std::uint8_t, storage::layout::kBlockSize> block{};
        block.fill(0xAB);
        storage::serialize(SaveData{}, block);

        // Everything past the payload and its checksum must be zeroed, not
        // left as whatever was in the buffer: leftovers would leak into the
        // checksum-free tail and make two identical saves compare differently
        // on device.
        for (std::size_t i = 73; i < block.size(); ++i) {
            CHECK(block[i] == 0);
        }
    }

    TEST_CASE("settings round-trip as flags") {
        HeadlessPlatform platform;
        SaveData quiet;
        quiet.settings = 0;
        CHECK_FALSE(quiet.soundEnabled());
        CHECK(storage::store(platform.storage(), quiet));

        SaveData read;
        CHECK(storage::load(platform.storage(), read) == LoadResult::Loaded);
        CHECK_FALSE(read.soundEnabled());

        CHECK(SaveData{}.soundEnabled()); // sound on by default
    }

    TEST_CASE("saving twice does not accumulate writes beyond the calls made") {
        // Flash and EEPROM wear out. A game that saves every tick would pass every
        // other test and destroy a device in a week.
        HeadlessPlatform platform;
        SaveData first;
        first.setHighScore(GameId::Shift, 1);
        first.settings = 1;
        SaveData second;
        second.setHighScore(GameId::Shift, 2);
        second.settings = 1;
        CHECK(storage::store(platform.storage(), first));
        CHECK(storage::store(platform.storage(), second));
        CHECK(platform.memoryStorage().writeCount() == 2);

        SaveData read;
        CHECK(storage::load(platform.storage(), read) == LoadResult::Loaded);
        CHECK(read.highScore(GameId::Shift) == 2);
    }

    TEST_CASE("the checksum matches a known CRC-16/CCITT-FALSE value") {
        // Pins the algorithm rather than the implementation: "123456789" is the
        // standard check vector for this variant.
        const std::array<std::uint8_t, 9> check{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
        CHECK(storage::checksum(check) == 0x29B1);
    }

} // TEST_SUITE
