#include "game/demo_scene.hpp"

#include "core/fixed_string.hpp"
#include "renderer/font.hpp"

#include <algorithm>

namespace wumpo::game {
namespace {

using input::Button;

constexpr int kPlayfieldTop = 7; // leaves a strip at the top for the score
constexpr int kMoveEveryTicks = 4;

/// Layout of the status strip. The score sits at the left; the timer bar starts
/// past it, because a full bar drawn from the screen edge covers the score
/// exactly when the run begins - which is when a player most needs to see it.
constexpr int kStatusTop = 0;
constexpr int kBarLeft = 12;
constexpr int kBarWidth = config::kScreenWidth - kBarLeft;
constexpr int kBarTop = 1;
constexpr int kBarHeight = 3;
constexpr int kRuleRow = kPlayfieldTop - 2;

/// A short blip on a score, a lower one when the run ends. Frequencies picked to
/// be audible on a piezo, which has nothing below a few hundred hertz.
constexpr DemoScene::Sound kScoreSound{.frequency_hz = 1'320, .duration_ms = 40};
constexpr DemoScene::Sound kOverSound{.frequency_hz = 330, .duration_ms = 220};

} // namespace

void DemoScene::reset(std::uint64_t seed) {
    seed_ = seed;
    rng_ = core::Pcg32(seed);
    player_x_ = config::kScreenWidth / 2;
    player_y_ = (kPlayfieldTop + config::kScreenHeight) / 2;
    score_ = 0;
    ticks_ = 0;
    ticks_left_ = kRunTicks;
    phase_ = Phase::Playing;
    placeTarget();
}

void DemoScene::placeTarget() {
    // Anywhere in the playfield that fits the whole target.
    target_x_ = rng_.nextRange(0, config::kScreenWidth - kTargetSize);
    target_y_ = rng_.nextRange(kPlayfieldTop, config::kScreenHeight - kTargetSize);
}

bool DemoScene::touchingTarget() const noexcept {
    return player_x_ < target_x_ + kTargetSize && target_x_ < player_x_ + kPlayerSize &&
           player_y_ < target_y_ + kTargetSize && target_y_ < player_y_ + kPlayerSize;
}

DemoScene::Sound DemoScene::tick(const input::InputState& input) {
    ++ticks_;

    if (phase_ == Phase::Over) {
        // A restarts. The press edge, not the held state, so holding A through
        // a game over does not restart repeatedly.
        if (input.pressed(Button::A)) {
            reset(seed_ + 1);
        }
        return Sound{};
    }

    // Movement is quantised to every few ticks: at 60 Hz, one pixel per tick
    // across a 64-pixel screen would cross it in a second.
    if (ticks_ % kMoveEveryTicks == 0) {
        int dx = 0;
        int dy = 0;
        if (input.down(Button::Left)) {
            --dx;
        }
        if (input.down(Button::Right)) {
            ++dx;
        }
        if (input.down(Button::Up)) {
            --dy;
        }
        if (input.down(Button::Down)) {
            ++dy;
        }

        player_x_ = std::clamp(player_x_ + dx, 0, config::kScreenWidth - kPlayerSize);
        player_y_ = std::clamp(player_y_ + dy, kPlayfieldTop, config::kScreenHeight - kPlayerSize);
    }

    Sound sound;
    if (touchingTarget()) {
        ++score_;
        high_score_ = std::max(high_score_, score_);
        placeTarget();
        sound = kScoreSound;
    }

    --ticks_left_;
    if (ticks_left_ <= 0) {
        phase_ = Phase::Over;
        sound = kOverSound;
    }
    return sound;
}

void DemoScene::render(renderer::Framebuffer& frame) const {
    frame.clear();

    // Text is built on the stack throughout: drawing happens every frame, and
    // std::to_string would allocate every one of them.
    if (phase_ == Phase::Over) {
        // 16 characters fit across the screen; centre each line by hand rather
        // than inventing a layout system nothing else needs yet.
        core::ScreenText score_text("SCORE ");
        score_text.append(score_);

        const auto centred = [](std::string_view text) {
            return (config::kScreenWidth - renderer::font::textWidth(text)) / 2;
        };

        renderer::drawText(frame, centred("TIME UP"), 8, "TIME UP");
        renderer::drawText(frame, centred(score_text.view()), 16, score_text.view());
        renderer::drawText(frame, centred("A=AGAIN"), 24, "A=AGAIN");
        return;
    }

    // Score at the left of the status strip, drawn from the very top row so its
    // bottom row clears the rule below.
    core::ScreenText score_text;
    score_text.append(score_);
    renderer::drawText(frame, 1, kStatusTop, score_text.view());

    // Remaining time as a bar that shortens from the left: readable at a glance,
    // which a number is not on a screen this size.
    const int bar_width = kBarWidth * ticks_left_ / kRunTicks;
    frame.fillRect(config::kScreenWidth - bar_width, kBarTop, bar_width, kBarHeight, true);

    frame.drawLine(0, kRuleRow, config::kScreenWidth - 1, kRuleRow, true);

    // The target is hollow, the player solid, so they stay distinguishable when
    // they overlap at three pixels across.
    frame.drawRect(target_x_, target_y_, kTargetSize, kTargetSize, true);
    frame.fillRect(player_x_, player_y_, kPlayerSize, kPlayerSize, true);
}

std::uint64_t DemoScene::stateHash() const noexcept {
    // FNV-1a over the fields that define a run.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };

    mix(static_cast<std::uint64_t>(player_x_));
    mix(static_cast<std::uint64_t>(player_y_));
    mix(static_cast<std::uint64_t>(target_x_));
    mix(static_cast<std::uint64_t>(target_y_));
    mix(static_cast<std::uint64_t>(score_));
    mix(static_cast<std::uint64_t>(ticks_));
    mix(static_cast<std::uint64_t>(ticks_left_));
    mix(static_cast<std::uint64_t>(phase_));
    mix(rng_.state());
    return hash;
}

} // namespace wumpo::game
