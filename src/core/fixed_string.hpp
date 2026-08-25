#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <string_view>

namespace wumpo::core {

/// A string builder with its storage on the stack.
///
/// Exists because `std::to_string` and `operator+` allocate, and the game draws
/// a score every single frame. On the desktop that is invisible; on a
/// microcontroller with no heap worth the name it is the difference between a
/// runtime that ports and one that does not.
///
/// Overflow truncates rather than growing or asserting. On a 64-pixel screen
/// nothing longer than sixteen characters can be displayed anyway, so a longer
/// string is already a layout bug - and losing its tail is a better failure than
/// an allocation or a crash on a device with no console to report it to.
template<std::size_t Capacity>
class FixedString {
public:
    constexpr FixedString() = default;

    explicit constexpr FixedString(std::string_view text) { append(text); }

    constexpr void append(std::string_view text) {
        for (const char character : text) {
            if (size_ >= Capacity) {
                truncated_ = true;
                return;
            }
            buffer_[size_++] = character;
        }
    }

    constexpr void append(char character) { append(std::string_view(&character, 1)); }

    /// Appends a decimal integer.
    void append(int value) {
        std::array<char, 12> digits{}; // -2147483648 plus room
        const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), value);
        if (result.ec != std::errc{}) {
            truncated_ = true;
            return;
        }
        append(
            std::string_view(digits.data(), static_cast<std::size_t>(result.ptr - digits.data())));
    }

    constexpr void clear() noexcept {
        size_ = 0;
        truncated_ = false;
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view(buffer_.data(), size_);
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    /// True if something did not fit. Worth checking in a test; not worth
    /// handling at a call site that is drawing a two-digit score.
    [[nodiscard]] constexpr bool truncated() const noexcept { return truncated_; }

private:
    std::array<char, Capacity> buffer_{};
    std::size_t size_ = 0;
    bool truncated_ = false;
};

/// Long enough for a full line of the device's 16-character text.
using ScreenText = FixedString<24>;

} // namespace wumpo::core
