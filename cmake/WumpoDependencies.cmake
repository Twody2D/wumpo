# External dependencies, fetched at configure time and pinned by tag.
#
# Only the desktop backend needs anything external. See THIRD_PARTY.md for the
# justification of each entry and docs/decisions/ADR-006-sdl3-over-sdl2.md for
# why this is SDL3 and not SDL2.

include(FetchContent)

set(WUMPO_SDL_TAG "release-3.4.14" CACHE STRING "Pinned SDL3 release tag")

# Trim SDL down to what the emulator actually uses: video, audio, events.
# Everything else is build time and binary size we do not need.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${WUMPO_SDL_TAG}
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    EXCLUDE_FROM_ALL
    SYSTEM)

FetchContent_MakeAvailable(SDL3)
