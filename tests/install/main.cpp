#include <axiom/core/core.hpp>

#include <cstdlib>
#include <string>

int main() {
    return std::string{axiom::core::frameworkName()} == "Axiom" ? EXIT_SUCCESS : EXIT_FAILURE;
}
