#include "platform/headless/headless_platform.hpp"
#include "storage/save_data.hpp"

#include <doctest.h>

#include <array>
#include <cstdint>

using wumpo::platform::headless::HeadlessPlatform;
using wumpo::storage::LoadResult;
using wumpo::storage::SaveData;
namespace storage = wumpo::storage;

TEST_SUITE("storage") {

    TEST_CASE("a save survives a round trip") {
        HeadlessPlatform platform;
        const SaveData written{.high_score = 1234, .settings = 0};

        CHECK(storage::store(platform.storage(), written));

        SaveData read;
        CHECK(storage::load(platform.storage(), read) == LoadResult::Loaded);
        CHECK(read == written);
    }

    TEST_CASE("a device that has never been written reports empty, not corrupt") {
        // What a brand new console looks like. Treating this as corruption would
        // show an error to every first-time player.
        HeadlessPlatform platform;
        SaveData data{.high_score = 99, .settings = 0};
        CHECK(storage::load(platform.storage(), data) == LoadResult::Empty);
        // A failed load must not have touched the caller's data.
        CHECK(data.high_score == 99);
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
        storage::serialize(SaveData{.high_score = 4242, .settings = 1}, block);

        SaveData data;
        REQUIRE(storage::deserialize(block, data) == LoadResult::Loaded);

        block[5] = static_cast<std::uint8_t>(block[5] ^ 0x01);
        CHECK(storage::deserialize(block, data) == LoadResult::BadChecksum);
    }

    TEST_CASE("corrupting the checksum itself is also caught") {
        std::array<std::uint8_t, storage::layout::kBlockSize> block{};
        storage::serialize(SaveData{.high_score = 7, .settings = 1}, block);
        block[10] = static_cast<std::uint8_t>(block[10] ^ 0xFF);

        SaveData data;
        CHECK(storage::deserialize(block, data) == LoadResult::BadChecksum);
    }

    TEST_CASE("a save from a future version is reported, not misread") {
        // Someone runs an older build after a newer one. Reading the newer layout as
        // if it were this one would silently corrupt their progress.
        std::array<std::uint8_t, storage::layout::kBlockSize> block{};
        storage::serialize(SaveData{.high_score = 500, .settings = 1}, block);

        block[4] = storage::layout::kVersion + 1;
        // Recompute the checksum so the block is valid, just newer.
        const std::uint16_t crc =
            storage::checksum(std::span<const std::uint8_t>(block).subspan(0, 10));
        block[10] = static_cast<std::uint8_t>(crc & 0xFFU);
        block[11] = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);

        SaveData data{.high_score = 1, .settings = 0};
        CHECK(storage::deserialize(block, data) == LoadResult::FutureVersion);
        CHECK(data.high_score == 1); // untouched
    }

    TEST_CASE("the serialized block is exactly the storage size") {
        std::array<std::uint8_t, storage::layout::kBlockSize> block{};
        block.fill(0xAB);
        storage::serialize(SaveData{}, block);

        // Everything past the payload must be zeroed, not left as whatever was in
        // the buffer: leftovers would leak into the checksum-free tail and make two
        // identical saves compare differently on device.
        for (std::size_t i = 12; i < block.size(); ++i) {
            CHECK(block[i] == 0);
        }
    }

    TEST_CASE("settings round-trip as flags") {
        HeadlessPlatform platform;
        SaveData quiet{.high_score = 0, .settings = 0};
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
        CHECK(storage::store(platform.storage(), SaveData{.high_score = 1, .settings = 1}));
        CHECK(storage::store(platform.storage(), SaveData{.high_score = 2, .settings = 1}));
        CHECK(platform.memoryStorage().writeCount() == 2);

        SaveData read;
        CHECK(storage::load(platform.storage(), read) == LoadResult::Loaded);
        CHECK(read.high_score == 2);
    }

    TEST_CASE("the checksum matches a known CRC-16/CCITT-FALSE value") {
        // Pins the algorithm rather than the implementation: "123456789" is the
        // standard check vector for this variant.
        const std::array<std::uint8_t, 9> check{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
        CHECK(storage::checksum(check) == 0x29B1);
    }

} // TEST_SUITE
