#include <axiom/axiom.hpp>

#include <string_view>

namespace axiom {

const char* frameworkName() noexcept { return "Axiom"; }

bool isFrameworkName(std::string_view candidate) noexcept { return candidate == frameworkName(); }

} // namespace axiom
