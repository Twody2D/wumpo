#pragma once

#include "game/echo.hpp"
#include "game/shift.hpp"
#include "input/input_state.hpp"
#include "renderer/framebuffer.hpp"
#include "storage/save_data.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <variant>

namespace wumpo::game {

/// Human-readable name for the selector screen. Independent of storage
/// ordering: this is display order, `GameId`'s numeric value is save order.
[[nodiscard]] std::string_view gameName(storage::GameId id) noexcept;

/// Wumpo's device-level shell: owns whichever game is currently active and the
/// hidden switcher that moves between games without a boot menu.
///
/// Mirrors the same `tick(input) -> Sound` / `render(Framebuffer&) const`
/// contract every game already has, so `emulator/main.cpp` drives this
/// exactly the way it drove a single game before - the launcher is one more
/// thing with that shape, not a different kind of thing.
///
/// See docs/decisions/ADR-008-multi-game-launcher.md.
class GameHost {
public:
    enum class Mode : std::uint8_t { Playing, Selecting };

    struct Sound {
        std::uint16_t frequency_hz = 0;
        std::uint16_t duration_ms = 0;

        [[nodiscard]] constexpr bool silent() const noexcept { return duration_ms == 0; }
    };

    /// How many games actually exist. Slots in storage::SaveData beyond this
    /// are reserved for later, not shown or selectable.
    static constexpr int kGameCount = 2;

    /// Ticks A and B must be held together before the switcher opens - long
    /// enough that no accidental double-tap opens it, per ADR-008.
    static constexpr int kSwitchHoldTicks = 90;

    /// `seed` starts a chain: every game entered, at boot or by switching,
    /// gets the next seed in the chain, so a whole device session replays
    /// exactly from one seed the way a single game's run always has.
    /// `save` seeds every game's high score and which one boots first.
    GameHost(std::uint64_t seed, const storage::SaveData& save);

    [[nodiscard]] Sound tick(const input::InputState& input);
    void render(renderer::Framebuffer& frame) const;

    [[nodiscard]] storage::GameId activeGame() const noexcept { return active_id_; }
    [[nodiscard]] Mode mode() const noexcept { return mode_; }

    // Not noexcept, unlike the plain field accessors above: each of these
    // dispatches through std::visit, which the standard does not specify as
    // noexcept (it would only actually throw if the variant were valueless,
    // which nothing here ever does to it).
    [[nodiscard]] int tickCount() const;
    [[nodiscard]] std::uint64_t seed() const;
    [[nodiscard]] int score() const;

    /// True once the active game has ended its run (crashed). Always false
    /// while `mode() == Selecting`, since no game is being played then.
    [[nodiscard]] bool gameOver() const;

    /// Restarts the active game at a specific seed, independent of the
    /// session's own seed chain. This is what a developer hotkey (not a
    /// recorded input) uses for a hard restart - see emulator/main.cpp.
    void restartActive(std::uint64_t seed);

    /// The active game's own high score. Kept in sync with the underlying
    /// game every tick, so this is always what a save should persist.
    [[nodiscard]] std::uint32_t highScore(storage::GameId game) const noexcept {
        return high_scores_[static_cast<std::size_t>(game)];
    }

    /// Every game's high score, indexed by `GameId`, for writing back to
    /// `storage::SaveData` in one pass - a game switched away from earlier in
    /// the session still needs its improved score persisted.
    [[nodiscard]] const std::array<std::uint32_t, storage::kMaxGames>& highScores() const noexcept {
        return high_scores_;
    }

    /// FNV-1a over everything that defines where this session is: the launcher
    /// state plus whichever game is active. Same pattern and same rationale as
    /// every individual game's `stateHash()`; not noexcept for the same
    /// std::visit reason as the accessors above.
    [[nodiscard]] std::uint64_t stateHash() const;

private:
    void enterGame(storage::GameId id);

    std::variant<ShiftGame, EchoGame> active_;
    storage::GameId active_id_ = storage::GameId::Shift;
    Mode mode_ = Mode::Playing;
    int cursor_ = 0;
    int chord_held_ticks_ = 0;
    std::uint64_t next_seed_;
    std::array<std::uint32_t, storage::kMaxGames> high_scores_{};
};

} // namespace wumpo::game
