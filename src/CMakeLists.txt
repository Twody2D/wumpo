# wumpo_core — everything that could one day run on the device.
#
# This target must never link or include SDL, Windows or any OS-specific header.
# CI builds it with WUMPO_BUILD_EMULATOR=OFF to prove that stays true.

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/core/version_generated.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/core/version_generated.hpp"
    @ONLY)

add_library(wumpo_core STATIC
    core/version.cpp
    renderer/framebuffer.cpp
)

add_library(wumpo::core ALIAS wumpo_core)

target_include_directories(wumpo_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(wumpo_core
    PRIVATE wumpo::warnings
    PUBLIC wumpo::sanitizers)

set_target_properties(wumpo_core PROPERTIES FOLDER "wumpo")

# The desktop backend is the only place SDL is allowed to exist.
if(WUMPO_BUILD_EMULATOR)
    add_subdirectory(platform/desktop)
endif()
