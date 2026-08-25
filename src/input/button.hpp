#pragma once

#include "core/config.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace wumpo::input {

/// The six physical buttons. The device has exactly these; there is no Start or
/// Select, and `A` doubles as "confirm" and "restart".
///
/// See docs/architecture/virtual-hardware.md.
enum class Button : std::uint8_t {
    Left = 0,
    Right = 1,
    Up = 2,
    Down = 3,
    A = 4,
    B = 5,
};

/// One bit per button, so a complete input state is one byte. This is also the
/// unit a replay records: everything else about input is derived from it.
using ButtonMask = std::uint8_t;

inline constexpr int kButtonCount = config::kButtonCount;
static_assert(kButtonCount == 6, "button names and masks assume six buttons");

[[nodiscard]] constexpr ButtonMask maskOf(Button button) noexcept {
    return static_cast<ButtonMask>(1U << static_cast<std::uint8_t>(button));
}

/// All six bits set: the mask with every button held.
inline constexpr ButtonMask kAllButtons = static_cast<ButtonMask>((1U << kButtonCount) - 1U);

[[nodiscard]] constexpr bool isSet(ButtonMask mask, Button button) noexcept {
    return (mask & maskOf(button)) != 0;
}

/// Stable uppercase names. These are written into replay files, so changing one
/// breaks every recording made before the change.
[[nodiscard]] constexpr std::string_view name(Button button) noexcept {
    switch (button) {
    case Button::Left:
        return "LEFT";
    case Button::Right:
        return "RIGHT";
    case Button::Up:
        return "UP";
    case Button::Down:
        return "DOWN";
    case Button::A:
        return "A";
    case Button::B:
        return "B";
    }
    return "";
}

/// Parses a button name. Returns nothing for anything unrecognized rather than
/// guessing: a replay naming a button we do not have is a corrupt replay.
[[nodiscard]] constexpr std::optional<Button> parseButton(std::string_view text) noexcept {
    if (text == "LEFT") {
        return Button::Left;
    }
    if (text == "RIGHT") {
        return Button::Right;
    }
    if (text == "UP") {
        return Button::Up;
    }
    if (text == "DOWN") {
        return Button::Down;
    }
    if (text == "A") {
        return Button::A;
    }
    if (text == "B") {
        return Button::B;
    }
    return std::nullopt;
}

/// Iteration helper so callers do not hard-code the button list.
inline constexpr std::array<Button, kButtonCount> kAllButtonList = {
    Button::Left, Button::Right, Button::Up, Button::Down, Button::A, Button::B};

} // namespace wumpo::input
