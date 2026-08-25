# Sanitizer configuration, opt-in through -DWUMPO_SANITIZE=address;undefined
#
# MSVC supports AddressSanitizer but has no UndefinedBehaviorSanitizer, which is
# why CI also runs a Linux/clang job. Requesting UBSan under MSVC is a warning,
# not an error, so a developer on Windows can still use the same preset.

add_library(wumpo_sanitizers INTERFACE)
add_library(wumpo::sanitizers ALIAS wumpo_sanitizers)

if(NOT WUMPO_SANITIZE)
    return()
endif()

if(MSVC)
    if("address" IN_LIST WUMPO_SANITIZE)
        target_compile_options(wumpo_sanitizers INTERFACE /fsanitize=address)
        # ASan on MSVC is incompatible with incremental linking and edit-and-continue.
        target_link_options(wumpo_sanitizers INTERFACE /INCREMENTAL:NO)
    endif()
    if("undefined" IN_LIST WUMPO_SANITIZE)
        message(WARNING "MSVC has no UndefinedBehaviorSanitizer; ignoring. Use the Linux CI job.")
    endif()
else()
    list(JOIN WUMPO_SANITIZE "," sanitize_list)
    target_compile_options(wumpo_sanitizers INTERFACE
        -fsanitize=${sanitize_list}
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all)
    target_link_options(wumpo_sanitizers INTERFACE -fsanitize=${sanitize_list})
endif()

message(STATUS "Sanitizers enabled: ${WUMPO_SANITIZE}")
