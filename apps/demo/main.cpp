#include <axiom/core/core.hpp>

#include <cstdio>
#include <cstdlib>

int main() { return std::puts(axiom::core::frameworkName()) == EOF ? EXIT_FAILURE : EXIT_SUCCESS; }
