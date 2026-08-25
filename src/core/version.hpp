#pragma once

namespace wumpo::core {

/// Build identity. Reported by the emulator and written into replay files so a
/// recording can be traced back to the build that produced it.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

[[nodiscard]] Version version() noexcept;

/// Formatted as "major.minor.patch".
[[nodiscard]] const char* versionString() noexcept;

} // namespace wumpo::core
