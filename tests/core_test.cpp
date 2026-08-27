#include <axiom/core/core.hpp>

#include <cstdlib>
#include <string>

int main() {
    if(std::string{axiom::core::frameworkName()} != "Axiom") {
        return EXIT_FAILURE;
    }
    if(!axiom::core::isFrameworkName("Axiom")) {
        return EXIT_FAILURE;
    }
    if(axiom::core::isFrameworkName("Other")) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
