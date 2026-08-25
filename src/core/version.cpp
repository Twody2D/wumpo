#include "core/version.hpp"

#include "core/version_generated.hpp"

namespace wumpo::core {

Version version() noexcept {
    return Version{.major = kVersionMajor, .minor = kVersionMinor, .patch = kVersionPatch};
}

const char* versionString() noexcept {
    return kVersionString;
}

} // namespace wumpo::core
