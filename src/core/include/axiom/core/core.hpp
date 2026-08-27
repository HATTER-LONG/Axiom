#pragma once

#include <string_view>

namespace axiom::core {

/** @brief Returns the framework name. */
const char* frameworkName() noexcept;

/** @brief Returns whether the candidate matches the framework name. */
bool isFrameworkName(std::string_view candidate) noexcept;

} // namespace axiom::core
