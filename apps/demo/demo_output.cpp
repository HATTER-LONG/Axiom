#include "demo_output.hpp"

#include <axiom/core/base/error.hpp>

#include <cstdio>
#include <string_view>

namespace axiom::demo {

void printView(const std::string_view text) {
    std::printf("%.*s", static_cast<int>(text.size()), text.data());
}

void printError(const core::Error& error) {
    std::printf("  -> %s", error.message.c_str());
    if(error.path) {
        std::printf(" (at %s)", error.path->c_str());
    }
    std::printf("\n");
}

} // namespace axiom::demo
