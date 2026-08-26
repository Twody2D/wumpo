#include "game/echo.hpp"

#include "core/fixed_string.hpp"
#include "renderer/font.hpp"

#include <algorithm>

namespace wumpo::game {
namespace {

using input::Button;

/// Screen layout, top to bottom: the ping-readiness bar owns row 0 outright,
/// the score sits below it, then a rule separates the HUD from the play field.
constexpr int kBarRow = 0;
constexpr int kScoreRow = 1;
constexpr int kRuleRow = EchoGame::kPlayfieldTop - 1;

constexpr int kInitialGapWidth = 14;
constexpr int kMinGapWidth = 8;
constexpr int kInitialFallEveryTicks = 8;
constexpr int kMinFallEveryTicks = 3;
/// How many pixels either side of the player's current position a fresh gap
/// may land. Bounded, not screen-wide: at the fall-speed floor a wall takes
/// (kPlayerRow - kPlayfieldTop) * kMinFallEveryTicks ticks to land, and this
/// jump must stay reachable within that budget even with zero ticks spent
/// waiting on the ping cooldown - see the comment on gap_x_ in echo.hpp.
constexpr int kMaxGapJump = 10;
/// Every this many points, the wall falls a tick faster and the gap narrows -
/// simple start, rising ceiling, per docs/game-design/principles.md.
constexpr int kSpeedupEveryScore = 3;

constexpr int kPingDurationTicks = 20;
constexpr int kPingCooldownTicks = 50;

// Sounds are shared verbatim with The Shift for pass and crash: a device-wide
// audio language, so the same event sounds the same regardless of which game
// is running. The ping click is Echo's own - it is the one place across
// Wumpo's games where a sound is not just feedback but the fiction itself.
constexpr EchoGame::Sound kPingSound{.frequency_hz = 900, .duration_ms = 15};
constexpr EchoGame::Sound kPassSound{.frequency_hz = 1'600, .duration_ms = 35};
constexpr EchoGame::Sound kCrashSound{.frequency_hz = 180, .duration_ms = 250};

} // namespace

void EchoGame::reset(std::uint64_t seed) {
    seed_ = seed;
    rng_ = core::Pcg32(seed);
    player_x_ = (config::kScreenWidth - kPlayerWidth) / 2;

    gap_width_ = kInitialGapWidth;
    fall_every_ticks_ = kInitialFallEveryTicks;
    ping_visible_remaining_ = 0;
    ping_cooldown_remaining_ = 0;

    score_ = 0;
    ticks_ = 0;
    phase_ = Phase::Playing;
    resetWall();
}

void EchoGame::resetWall() noexcept {
    wall_y_ = kPlayfieldTop;
    fall_countdown_ = fall_every_ticks_;
    // A fresh offset every wall, not a slide: The Shift's gap moves in a
    // pattern you can learn to read on sight, but here sight is exactly what
    // is taken away, so the direction and size of the jump are randomised
    // every time. It is bounded to kMaxGapJump around the player's own
    // position rather than screen-wide, so it always stays reachable - see
    // the comment on kMaxGapJump above.
    const int max_gap_x = config::kScreenWidth - gap_width_;
    const int jump = rng_.nextRange(-kMaxGapJump, kMaxGapJump);
    gap_x_ = std::clamp(player_x_ + jump, 0, max_gap_x);
}

bool EchoGame::playerClearsGap() const noexcept {
    return player_x_ >= gap_x_ && player_x_ + kPlayerWidth <= gap_x_ + gap_width_;
}

EchoGame::Sound EchoGame::tick(const input::InputState& input) {
    ++ticks_;

    if (phase_ == Phase::Over) {
        // A restarts. The press edge, not the held state, so holding A through
        // a crash does not restart repeatedly.
        if (input.pressed(Button::A)) {
            reset(seed_ + 1);
        }
        return Sound{};
    }

    if (ticks_ % kMoveEveryTicks == 0) {
        int dx = 0;
        if (input.down(Button::Left)) {
            --dx;
        }
        if (input.down(Button::Right)) {
            ++dx;
        }
        player_x_ = std::clamp(player_x_ + dx, 0, config::kScreenWidth - kPlayerWidth);
    }

    if (ping_cooldown_remaining_ > 0) {
        --ping_cooldown_remaining_;
    }
    if (ping_visible_remaining_ > 0) {
        --ping_visible_remaining_;
    }

    bool pinged = false;
    if (input.pressed(Button::A) && ping_cooldown_remaining_ <= 0) {
        ping_visible_remaining_ = kPingDurationTicks;
        ping_cooldown_remaining_ = kPingCooldownTicks;
        pinged = true;
    }

    if (--fall_countdown_ <= 0) {
        fall_countdown_ = fall_every_ticks_;
        ++wall_y_;
        if (wall_y_ >= kPlayerRow) {
            if (playerClearsGap()) {
                ++score_;
                high_score_ = std::max(high_score_, score_);
                if (score_ % kSpeedupEveryScore == 0) {
                    fall_every_ticks_ = std::max(kMinFallEveryTicks, fall_every_ticks_ - 1);
                    gap_width_ = std::max(kMinGapWidth, gap_width_ - 1);
                }
                resetWall();
                return kPassSound;
            }
            phase_ = Phase::Over;
            return kCrashSound;
        }
    }

    return pinged ? kPingSound : Sound{};
}

void EchoGame::render(renderer::Framebuffer& frame) const {
    frame.clear();

    if (phase_ == Phase::Over) {
        // 16 characters fit across the screen; centre each line by hand rather
        // than inventing a layout system nothing else needs yet.
        core::ScreenText score_text("SCORE ");
        score_text.append(score_);
        core::ScreenText best_text("BEST ");
        best_text.append(high_score_);

        const auto centred = [](std::string_view text) {
            return (config::kScreenWidth - renderer::font::textWidth(text)) / 2;
        };

        renderer::drawText(frame, centred("CRASHED"), 1, "CRASHED");
        renderer::drawText(frame, centred(score_text.view()), 8, score_text.view());
        renderer::drawText(frame, centred(best_text.view()), 15, best_text.view());
        renderer::drawText(frame, centred("A=AGAIN"), 22, "A=AGAIN");
        return;
    }

    // The bar fills as the cooldown clears and snaps back the instant a ping
    // fires, so it doubles as a readable "when can I look again" countdown.
    const int elapsed = kPingCooldownTicks - ping_cooldown_remaining_;
    const int bar_width =
        std::clamp(config::kScreenWidth * elapsed / kPingCooldownTicks, 0, config::kScreenWidth);
    frame.fillRect(0, kBarRow, bar_width, 1, true);

    core::ScreenText score_text;
    score_text.append(score_);
    renderer::drawText(frame, 1, kScoreRow, score_text.view());

    frame.drawLine(0, kRuleRow, config::kScreenWidth - 1, kRuleRow, true);

    // The wall exists the whole time - it can still be fallen into unseen -
    // but only draws while a ping is lighting it. Everything else in the play
    // field stays black.
    if (ping_visible_remaining_ > 0) {
        if (gap_x_ > 0) {
            frame.fillRect(0, wall_y_, gap_x_, kWallHeight, true);
        }
        const int gap_right = gap_x_ + gap_width_;
        if (gap_right < config::kScreenWidth) {
            frame.fillRect(gap_right, wall_y_, config::kScreenWidth - gap_right, kWallHeight, true);
        }
    }

    // The player's own marker is always visible - a game about darkness is
    // not a game about not knowing where you are.
    frame.fillRect(player_x_, kPlayerRow, kPlayerWidth, kPlayerHeight, true);
}

std::uint64_t EchoGame::stateHash() const noexcept {
    // FNV-1a over the fields that define a run. high_score_ is deliberately
    // excluded - see the constructor comment in echo.hpp.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };

    mix(static_cast<std::uint64_t>(player_x_));
    mix(static_cast<std::uint64_t>(wall_y_));
    mix(static_cast<std::uint64_t>(gap_x_));
    mix(static_cast<std::uint64_t>(gap_width_));
    mix(static_cast<std::uint64_t>(fall_every_ticks_));
    mix(static_cast<std::uint64_t>(fall_countdown_));
    mix(static_cast<std::uint64_t>(ping_visible_remaining_));
    mix(static_cast<std::uint64_t>(ping_cooldown_remaining_));
    mix(static_cast<std::uint64_t>(score_));
    mix(static_cast<std::uint64_t>(ticks_));
    mix(static_cast<std::uint64_t>(phase_));
    mix(rng_.state());
    return hash;
}

} // namespace wumpo::game
