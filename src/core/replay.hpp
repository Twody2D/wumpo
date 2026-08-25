#pragma once

#include "input/button.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wumpo::core {

/// A recorded run: a seed and one held-button mask per simulation tick.
///
/// That pair is enough to reproduce a run exactly, because the simulation is
/// deterministic and takes nothing else - no wall clock, no frame rate, no
/// platform state. It is also enough to reproduce a bug report of the form
/// "everything breaks on tick 183" from a seed, a replay and a build version.
struct Replay {
    /// Bumped whenever the meaning of the fields changes. Stored in the file so
    /// an old recording can be recognised rather than misread.
    static constexpr int kFormatVersion = 1;

    int version = kFormatVersion;
    std::uint64_t seed = 0;

    /// One entry per tick. Written as button names joined by '+', an empty
    /// string for a tick with nothing held.
    ///
    /// The design sketch showed `["LEFT", "LEFT", "A", "RIGHT"]`, which cannot
    /// express "nothing held" or two buttons at once; both happen constantly.
    /// This keeps that example valid - four ticks, one button each - while
    /// making the missing cases expressible.
    std::vector<input::ButtonMask> inputs;

    [[nodiscard]] bool operator==(const Replay&) const noexcept = default;
};

/// "" for nothing held, "A" for one button, "LEFT+A" for several. Button order
/// is fixed, so the same mask always writes the same text and two recordings of
/// the same run are identical files.
[[nodiscard]] std::string maskToText(input::ButtonMask mask);

/// Parses the above. Returns nothing for an unknown button name or malformed
/// separator: a replay naming a button the device does not have is corrupt, and
/// guessing would silently produce a different run.
[[nodiscard]] std::optional<input::ButtonMask> maskFromText(std::string_view text);

[[nodiscard]] std::string toJson(const Replay& replay);

/// Parses a replay file. Returns false with a message on anything malformed;
/// `replay` is left untouched.
///
/// Hand-written rather than pulled from a JSON library: the schema is three
/// fields and will not grow, and this is the only externally-supplied format the
/// project reads - so it is also the only real attack surface. A hundred lines
/// that can be read in full beat a dependency that cannot.
[[nodiscard]] bool fromJson(std::string_view text, Replay& replay, std::string* error);

} // namespace wumpo::core
