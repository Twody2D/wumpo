// Placeholder translation unit: the SDL3 backend lands here in the next step.
// It exists so the target links and the SDL dependency is verified end to end
// before any real backend code depends on it.

#include <SDL3/SDL.h>

namespace wumpo::platform::desktop {

/// Reports the SDL version the emulator was compiled against.
int compiledSdlVersion() noexcept {
    return SDL_VERSION;
}

}  // namespace wumpo::platform::desktop
