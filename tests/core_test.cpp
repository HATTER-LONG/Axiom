#include <axiom/core/core.hpp>

#include <cstdlib>
#include <string_view>

int main() {
    if(std::string_view{axiom::core::frameworkName()} != "Axiom") {
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
