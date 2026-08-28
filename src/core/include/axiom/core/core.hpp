#pragma once

#include <string_view>

namespace axiom::core {

/**
 * @brief Returns the framework name.
 *
 * @return A pointer to a null-terminated string with static storage duration.
 */
const char* frameworkName() noexcept;

/**
 * @brief Tests whether a candidate equals the framework name.
 *
 * @param candidate Text to compare with the framework name.
 * @return `true` when the texts match; otherwise `false`.
 */
bool isFrameworkName(std::string_view candidate) noexcept;

} // namespace axiom::core
