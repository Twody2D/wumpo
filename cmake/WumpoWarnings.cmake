# Warning configuration shared by every first-party target.
#
# Warnings are not noise to be silenced: on a project that must survive a move to
# a microcontroller, a conversion or sign-compare warning is usually a real bug.

add_library(wumpo_warnings INTERFACE)
add_library(wumpo::warnings ALIAS wumpo_warnings)

if(MSVC)
    target_compile_options(wumpo_warnings INTERFACE
        /W4
        /permissive-       # standard-conformant parsing
        /w14242 /w14254    # lossy conversions
        /w14263 /w14265    # virtual function mismatches
        /w14287 /w14296    # unsigned/negative comparison problems
        /w14311 /w14545 /w14546 /w14547 /w14549 /w14555
        /w14619 /w14640 /w14826 /w14905 /w14906 /w14928
        /utf-8
        /Zc:__cplusplus    # report the real standard version
        /Zc:preprocessor)
    if(WUMPO_WERROR)
        target_compile_options(wumpo_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(wumpo_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough)
    if(WUMPO_WERROR)
        target_compile_options(wumpo_warnings INTERFACE -Werror)
    endif()
endif()
