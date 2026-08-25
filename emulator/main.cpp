#include "core/config.hpp"
#include "core/version.hpp"

#include <cstdio>

namespace wumpo::platform::desktop {
int compiledSdlVersion() noexcept;
}

int main() {
    std::printf("Wumpo %s\n", wumpo::core::versionString());
    std::printf("screen   %dx%d monochrome, %d bytes\n", wumpo::config::kScreenWidth,
                wumpo::config::kScreenHeight, wumpo::config::kFramebufferBytes);
    std::printf("tick     %d Hz\n", wumpo::config::kTickHz);
    std::printf("SDL      %d\n", wumpo::platform::desktop::compiledSdlVersion());
    return 0;
}
