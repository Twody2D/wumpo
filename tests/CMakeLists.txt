# One test binary, several CTest entries filtered by doctest test suite.
#
# doctest is vendored as a single header (see ADR-007), so there is no external
# CMake project here and the test build works with no network access.

add_library(wumpo_doctest INTERFACE)
target_include_directories(wumpo_doctest SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

add_executable(wumpo_tests
    test_main.cpp
    unit/test_version.cpp
)

target_link_libraries(wumpo_tests
    PRIVATE wumpo::core wumpo_doctest wumpo::warnings)

target_include_directories(wumpo_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(wumpo_tests PROPERTIES FOLDER "wumpo/tests")

# Each suite becomes its own CTest entry, so `ctest -R golden` and friends work
# and a failure names the area rather than "the tests".
set(WUMPO_TEST_SUITES core renderer input audio storage game determinism golden replay alloc)

foreach(suite IN LISTS WUMPO_TEST_SUITES)
    add_test(NAME ${suite} COMMAND wumpo_tests --test-suite=${suite})
    set_tests_properties(${suite} PROPERTIES LABELS "${suite}")
endforeach()

# Guards against a suite existing in the binary but not in the list above, which
# would mean tests that compile in CI and never run.
add_test(NAME suite-registration COMMAND "${CMAKE_COMMAND}"
    "-DTESTS_EXE=$<TARGET_FILE:wumpo_tests>"
    "-DEXPECTED_SUITES=${WUMPO_TEST_SUITES}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckSuites.cmake")
