#include <testlib/core/core.hpp>

#include <cstdlib>

int main() { return testlib::core::version() > 0 ? EXIT_SUCCESS : EXIT_FAILURE; }
