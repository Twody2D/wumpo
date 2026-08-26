#include "game/shift.hpp"

#include "core/fixed_string.hpp"
#include "renderer/font.hpp"

#include <algorithm>

namespace wumpo::game {
namespace {

using input::Button;

/// Screen layout, top to bottom: the beat bar owns row 0 outright, the score
/// sits below it, then a rule separates the HUD from the falling wall.
constexpr int kBeatBarRow = 0;
constexpr int kScoreRow = 1;
constexpr int kRuleRow = ShiftGame::kPlayfieldTop - 1;

constexpr int kMaxGapX = config::kScreenWidth - ShiftGame::kGapWidth;
constexpr int kShiftStep = 6;

constexpr int kInitialFallEveryTicks = 6;
constexpr int kMinFallEveryTicks = 2;
constexpr int kInitialShiftPeriodTicks = 90;
constexpr int kMinShiftPeriodTicks = 40;
/// Every this many points, the wall falls a tick faster and the beat shortens
/// - simple start, rising ceiling, per docs/game-design/principles.md.
constexpr int kSpeedupEveryScore = 3;

constexpr ShiftGame::Sound kPassSound{.frequency_hz = 1'600, .duration_ms = 35};
constexpr ShiftGame::Sound kBeatSound{.frequency_hz = 600, .duration_ms = 12};
constexpr ShiftGame::Sound kCrashSound{.frequency_hz = 180, .duration_ms = 250};

} // namespace

void ShiftGame::reset(std::uint64_t seed) {
    seed_ = seed;
    rng_ = core::Pcg32(seed);
    player_x_ = (config::kScreenWidth - kPlayerWidth) / 2;

    // The gap and its direction are not reseeded by resetWall(): the shift is
    // one continuous process the walls merely fall through, not a fresh draw
    // per wall.
    gap_x_ = rng_.nextRange(0, kMaxGapX);
    gap_target_x_ = gap_x_;
    gap_direction_ = rng_.nextBool() ? 1 : -1;
    slide_ticks_remaining_ = 0;
    shift_period_ticks_ = kInitialShiftPeriodTicks;
    shift_countdown_ = shift_period_ticks_;

    fall_every_ticks_ = kInitialFallEveryTicks;
    score_ = 0;
    ticks_ = 0;
    phase_ = Phase::Playing;
    resetWall();
}

void ShiftGame::resetWall() noexcept {
    wall_y_ = kPlayfieldTop;
    fall_countdown_ = fall_every_ticks_;
}

void ShiftGame::beginShift() noexcept {
    // A reflection off the edges rather than a wraparound: a gap that
    // teleports from one edge to the other is unreadable, while a bounce
    // stays a single predictable pattern to watch and plan around. The move
    // itself is likewise spread over kShiftStep ticks in tick() rather than
    // applied here in one frame - a jump gives the eye nothing to read a
    // direction from.
    int target = gap_x_ + gap_direction_ * kShiftStep;
    if (target < 0) {
        target = -target;
        gap_direction_ = 1;
    } else if (target > kMaxGapX) {
        target = 2 * kMaxGapX - target;
        gap_direction_ = -1;
    }
    gap_target_x_ = target;
    // No faster than the player's own top speed (one pixel every
    // kMoveEveryTicks ticks): a slide the player cannot physically keep pace
    // with, even reacting instantly, is not a puzzle to read - it is a coin
    // flip decided before the player can respond.
    slide_ticks_remaining_ = kShiftStep * ShiftGame::kMoveEveryTicks;
}

bool ShiftGame::playerClearsGap() const noexcept {
    return player_x_ >= gap_x_ && player_x_ + kPlayerWidth <= gap_x_ + kGapWidth;
}

ShiftGame::Sound ShiftGame::tick(const input::InputState& input) {
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

    // A pulse starts a slide toward the new target; the slide itself advances
    // here too, before the fall check, so a wall that arrives mid-slide is
    // judged against wherever the gap has visibly moved to by that exact
    // tick - never a value it hasn't been drawn at.
    bool pulsed = false;
    if (--shift_countdown_ <= 0) {
        beginShift();
        shift_countdown_ = shift_period_ticks_;
        pulsed = true;
    }
    if (slide_ticks_remaining_ > 0) {
        --slide_ticks_remaining_;
        if (slide_ticks_remaining_ % kMoveEveryTicks == 0 && gap_x_ != gap_target_x_) {
            gap_x_ += (gap_target_x_ > gap_x_) ? 1 : -1;
        }
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
                    shift_period_ticks_ = std::max(kMinShiftPeriodTicks, shift_period_ticks_ - 5);
                }
                resetWall();
                return kPassSound;
            }
            phase_ = Phase::Over;
            return kCrashSound;
        }
    }

    return pulsed ? kBeatSound : Sound{};
}

void ShiftGame::render(renderer::Framebuffer& frame) const {
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

    // The beat bar fills over one shift period and snaps back at the pulse,
    // so it doubles as a readable countdown to the next gap movement.
    const int elapsed = shift_period_ticks_ - shift_countdown_;
    const int bar_width =
        std::clamp(config::kScreenWidth * elapsed / shift_period_ticks_, 0, config::kScreenWidth);
    frame.fillRect(0, kBeatBarRow, bar_width, 1, true);

    core::ScreenText score_text;
    score_text.append(score_);
    renderer::drawText(frame, 1, kScoreRow, score_text.view());

    frame.drawLine(0, kRuleRow, config::kScreenWidth - 1, kRuleRow, true);

    // The wall is everything except the gap: two rectangles, either of which
    // may be empty when the gap sits flush against an edge.
    if (gap_x_ > 0) {
        frame.fillRect(0, wall_y_, gap_x_, kWallHeight, true);
    }
    const int gap_right = gap_x_ + kGapWidth;
    if (gap_right < config::kScreenWidth) {
        frame.fillRect(gap_right, wall_y_, config::kScreenWidth - gap_right, kWallHeight, true);
    }

    frame.fillRect(player_x_, kPlayerRow, kPlayerWidth, kPlayerHeight, true);
}

std::uint64_t ShiftGame::stateHash() const noexcept {
    // FNV-1a over the fields that define a run. high_score_ is deliberately
    // excluded - see the constructor comment in shift.hpp.
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
    mix(static_cast<std::uint64_t>(gap_target_x_));
    mix(static_cast<std::uint64_t>(gap_direction_));
    mix(static_cast<std::uint64_t>(slide_ticks_remaining_));
    mix(static_cast<std::uint64_t>(fall_every_ticks_));
    mix(static_cast<std::uint64_t>(shift_period_ticks_));
    mix(static_cast<std::uint64_t>(fall_countdown_));
    mix(static_cast<std::uint64_t>(shift_countdown_));
    mix(static_cast<std::uint64_t>(score_));
    mix(static_cast<std::uint64_t>(ticks_));
    mix(static_cast<std::uint64_t>(phase_));
    mix(rng_.state());
    return hash;
}

} // namespace wumpo::game
