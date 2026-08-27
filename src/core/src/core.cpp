#include <axiom/core/core.hpp>

#include <string>

namespace axiom::core {

const char* frameworkName() noexcept { return "Axiom"; }

bool isFrameworkName(std::string_view candidate) noexcept { return candidate == frameworkName(); }

} // namespace axiom::core
