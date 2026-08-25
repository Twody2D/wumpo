# The SDL3 backend. The only target in the project that sees an SDL header.

include(WumpoDependencies)

add_library(wumpo_platform_desktop STATIC
    desktop_placeholder.cpp
)

add_library(wumpo::platform_desktop ALIAS wumpo_platform_desktop)

target_include_directories(wumpo_platform_desktop PUBLIC "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(wumpo_platform_desktop
    PUBLIC wumpo::core SDL3::SDL3-static
    PRIVATE wumpo::warnings)

set_target_properties(wumpo_platform_desktop PROPERTIES FOLDER "wumpo")
