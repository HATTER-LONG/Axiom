#pragma once

#include <axiom/core/action/action_id.hpp>
#include <axiom/core/action/descriptor.hpp>
#include <axiom/core/action/invocation_context.hpp>
#include <axiom/core/action/module.hpp>
#include <axiom/core/action/module_builder.hpp>
#include <axiom/core/action/runtime.hpp>
#include <axiom/core/base/error.hpp>
#include <axiom/core/base/result.hpp>
#include <axiom/core/base/type_descriptor.hpp>
#include <axiom/core/base/value.hpp>

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
