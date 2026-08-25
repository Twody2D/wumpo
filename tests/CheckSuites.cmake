# Fails if the test binary contains a doctest suite that CMake does not register
# as a CTest entry. Without this, adding a new TEST_SUITE would silently create
# tests that CI builds but never runs.
#
# Invoked as: cmake -DTESTS_EXE=... -DEXPECTED_SUITES=... -P CheckSuites.cmake

# Sent as a comma-separated string so the shell cannot split it; restore the list.
string(REPLACE "," ";" EXPECTED_SUITES "${EXPECTED_SUITES}")

execute_process(
    COMMAND "${TESTS_EXE}" --list-test-suites
    OUTPUT_VARIABLE listing
    RESULT_VARIABLE status
    OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT status EQUAL 0)
    message(FATAL_ERROR "Could not list test suites (exit ${status}):\n${listing}")
endif()

string(REPLACE "\r" "" listing "${listing}")
string(REPLACE "\n" ";" lines "${listing}")

# doctest frames the suite names with banner lines and brackets its own notes.
set(found "")
foreach(line IN LISTS lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "")
        continue()
    endif()
    string(FIND "${line}" "[doctest]" doctest_note)
    if(doctest_note EQUAL 0 OR line MATCHES "^=+$")
        continue()
    endif()
    list(APPEND found "${line}")
endforeach()

set(unregistered "")
foreach(suite IN LISTS found)
    if(NOT suite IN_LIST EXPECTED_SUITES)
        list(APPEND unregistered "${suite}")
    endif()
endforeach()

if(unregistered)
    message(FATAL_ERROR
        "Test suite(s) not registered with CTest: ${unregistered}\n"
        "Add them to WUMPO_TEST_SUITES in tests/CMakeLists.txt.")
endif()

message(STATUS "Registered suites present in the binary: ${found}")
