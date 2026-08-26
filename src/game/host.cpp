#include "game/host.hpp"

#include "core/config.hpp"
#include "core/fixed_string.hpp"
#include "renderer/font.hpp"

#include <algorithm>
#include <array>
#include <type_traits>

namespace wumpo::game {
namespace {

using input::Button;

/// Display order for the selector screen. Independent of `GameId`'s numeric
/// value, which is save-file order and must never change; this can be
/// reordered freely.
constexpr std::array<storage::GameId, GameHost::kGameCount> kGameOrder = {
    storage::GameId::Shift,
    storage::GameId::Echo,
};

int indexOf(storage::GameId id) noexcept {
    for (int i = 0; i < GameHost::kGameCount; ++i) {
        if (kGameOrder[static_cast<std::size_t>(i)] == id) {
            return i;
        }
    }
    return 0;
}

constexpr int kTitleRow = 1;
constexpr int kListStartRow = 10;
constexpr int kRowHeight = 7;
/// How many entries show at once. Fixed and small rather than "however many
/// games exist": the point is that a tenth game never needs this rewritten,
/// only scrolled to.
constexpr int kVisibleRows = 4;

} // namespace

std::string_view gameName(storage::GameId id) noexcept {
    switch (id) {
    case storage::GameId::Shift:
        return "SHIFT";
    case storage::GameId::Echo:
        return "ECHO";
    }
    return "";
}

GameHost::GameHost(std::uint64_t seed, const storage::SaveData& save)
    : next_seed_(seed), high_scores_(save.high_scores) {
    enterGame(save.last_played);
}

void GameHost::enterGame(storage::GameId id) {
    const std::uint64_t game_seed = next_seed_++;
    const auto initial_high_score = static_cast<int>(high_scores_[static_cast<std::size_t>(id)]);
    switch (id) {
    case storage::GameId::Shift:
        active_.emplace<ShiftGame>(game_seed, initial_high_score);
        break;
    case storage::GameId::Echo:
        active_.emplace<EchoGame>(game_seed, initial_high_score);
        break;
    }
    active_id_ = id;
}

GameHost::Sound GameHost::tick(const input::InputState& input) {
    if (mode_ == Mode::Selecting) {
        if (input.pressed(Button::Down)) {
            cursor_ = (cursor_ + 1) % kGameCount;
        }
        if (input.pressed(Button::Up)) {
            cursor_ = (cursor_ - 1 + kGameCount) % kGameCount;
        }
        if (input.pressed(Button::A)) {
            enterGame(kGameOrder[static_cast<std::size_t>(cursor_)]);
            mode_ = Mode::Playing;
        } else if (input.pressed(Button::B)) {
            mode_ = Mode::Playing;
        }
        return Sound{};
    }

    // Held, not pressed: this is a hold-to-open gesture, not a keypress, and
    // it must survive across ticks to accumulate toward kSwitchHoldTicks.
    chord_held_ticks_ =
        (input.down(Button::A) && input.down(Button::B)) ? chord_held_ticks_ + 1 : 0;
    if (chord_held_ticks_ >= kSwitchHoldTicks) {
        mode_ = Mode::Selecting;
        chord_held_ticks_ = 0;
        cursor_ = indexOf(active_id_);
        return Sound{};
    }

    const Sound sound = std::visit(
        [&input](auto& game) -> Sound {
            const auto game_sound = game.tick(input);
            return Sound{.frequency_hz = game_sound.frequency_hz,
                         .duration_ms = game_sound.duration_ms};
        },
        active_);
    high_scores_[static_cast<std::size_t>(active_id_)] = std::visit(
        [](auto& game) { return static_cast<std::uint32_t>(game.highScore()); }, active_);
    return sound;
}

void GameHost::render(renderer::Framebuffer& frame) const {
    if (mode_ != Mode::Selecting) {
        std::visit([&frame](auto& game) { game.render(frame); }, active_);
        return;
    }

    frame.clear();
    const int title_x = (config::kScreenWidth - renderer::font::textWidth("GAMES")) / 2;
    renderer::drawText(frame, title_x, kTitleRow, "GAMES");

    const int window = std::min(kGameCount, kVisibleRows);
    const int max_first = std::max(0, kGameCount - window);
    const int first = std::clamp(cursor_ - (window / 2), 0, max_first);

    for (int row = 0; row < window; ++row) {
        const int index = first + row;
        core::ScreenText line(index == cursor_ ? "> " : "  ");
        line.append(gameName(kGameOrder[static_cast<std::size_t>(index)]));
        renderer::drawText(frame, 4, kListStartRow + (row * kRowHeight), line.view());
    }
}

int GameHost::tickCount() const {
    return std::visit([](auto& game) { return game.tickCount(); }, active_);
}

std::uint64_t GameHost::seed() const {
    return std::visit([](auto& game) { return game.seed(); }, active_);
}

int GameHost::score() const {
    return std::visit([](auto& game) { return game.score(); }, active_);
}

bool GameHost::gameOver() const {
    return std::visit(
        [](auto& game) { return game.phase() == std::decay_t<decltype(game)>::Phase::Over; },
        active_);
}

void GameHost::restartActive(std::uint64_t seed) {
    next_seed_ = seed;
    enterGame(active_id_);
}

std::uint64_t GameHost::stateHash() const {
    // FNV-1a, same construction as every individual game's stateHash(): the
    // launcher's own state mixed with whichever game is currently active.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };

    mix(static_cast<std::uint64_t>(mode_));
    mix(static_cast<std::uint64_t>(cursor_));
    mix(static_cast<std::uint64_t>(active_id_));
    mix(std::visit([](auto& game) { return game.stateHash(); }, active_));
    return hash;
}

} // namespace wumpo::game
