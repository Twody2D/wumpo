// The single translation unit that compiles doctest itself. Every other test
// file includes the header only, so this cost is paid once.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
