#include <axiom/core/core.hpp>

#include <cstdio>

int main() { return std::puts(axiom::core::frameworkName()) == EOF; }
