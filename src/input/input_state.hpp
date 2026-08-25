#pragma once

#include "input/button.hpp"

namespace wumpo::input {

/// Button state for one simulation tick: what is held, what became held this
/// tick, and what was let go this tick.
///
/// Only `down` comes from the outside. `pressed` and `released` are derived here
/// from the previous tick, which is what makes replays exact: a recording stores
/// raw held-masks, and replaying them reconstructs the edges identically. If a
/// backend reported edges itself, a dropped or duplicated frame would silently
/// change them.
class InputState {
public:
    /// Advances one tick. Call exactly once per simulation tick, never per frame:
    /// calling it twice in a tick would consume the edges before the game sees
    /// them, and a button tap would vanish.
    constexpr void update(ButtonMask held) noexcept {
        held &= kAllButtons; // ignore bits no physical button maps to
        pressed_ = static_cast<ButtonMask>(held & ~down_);
        released_ = static_cast<ButtonMask>(down_ & ~held);
        down_ = held;
    }

    [[nodiscard]] constexpr bool down(Button button) const noexcept { return isSet(down_, button); }

    /// True only on the tick the button went down. This is what menus and jumps
    /// use; holding a button must not repeat unless the game asks for it.
    [[nodiscard]] constexpr bool pressed(Button button) const noexcept {
        return isSet(pressed_, button);
    }

    [[nodiscard]] constexpr bool released(Button button) const noexcept {
        return isSet(released_, button);
    }

    [[nodiscard]] constexpr bool anyPressed() const noexcept { return pressed_ != 0; }

    [[nodiscard]] constexpr ButtonMask downMask() const noexcept { return down_; }
    [[nodiscard]] constexpr ButtonMask pressedMask() const noexcept { return pressed_; }
    [[nodiscard]] constexpr ButtonMask releasedMask() const noexcept { return released_; }

    /// Clears everything, including history. Used when starting a run so a
    /// button held through a restart does not read as a fresh press.
    constexpr void reset() noexcept {
        down_ = 0;
        pressed_ = 0;
        released_ = 0;
    }

    [[nodiscard]] constexpr bool operator==(const InputState&) const noexcept = default;

private:
    ButtonMask down_ = 0;
    ButtonMask pressed_ = 0;
    ButtonMask released_ = 0;
};

} // namespace wumpo::input
