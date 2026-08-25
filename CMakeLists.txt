cmake_minimum_required(VERSION 3.24)

project(wumpo
    VERSION 0.1.0
    DESCRIPTION "Desktop prototype of a keychain-sized handheld console"
    LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
    endif()
endif()

option(WUMPO_BUILD_EMULATOR "Build the SDL3 desktop emulator" ON)
option(WUMPO_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(WUMPO_WERROR "Treat compiler warnings as errors" OFF)
set(WUMPO_SANITIZE "" CACHE STRING "Sanitizers: address, undefined, or address;undefined")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(WumpoWarnings)
include(WumpoSanitizers)

add_subdirectory(src)

if(WUMPO_BUILD_EMULATOR)
    add_subdirectory(emulator)
endif()

if(WUMPO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "wumpo ${PROJECT_VERSION}"
    " | emulator: ${WUMPO_BUILD_EMULATOR}"
    " | tests: ${WUMPO_BUILD_TESTS}"
    " | sanitizers: '${WUMPO_SANITIZE}'")
