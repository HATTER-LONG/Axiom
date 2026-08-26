#include <axiom/core/core.hpp>

#include <string_view>

int main() { return std::string_view{axiom::core::frameworkName()} == "Axiom" ? 0 : 1; }
