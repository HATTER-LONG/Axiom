#pragma once

/**
 * @file core.hpp
 * @brief Umbrella header for the Axiom Core public surface (base, action, logging).
 *
 * Prefer including this header from application and test code. Module-internal
 * translation units may include the narrower headers they need.
 */

// IWYU pragma: begin_exports
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
#include <axiom/core/logging/callback_sink.hpp>
#include <axiom/core/logging/console_sink.hpp>
#include <axiom/core/logging/log_collector.hpp>
#include <axiom/core/logging/log_filter.hpp>
#include <axiom/core/logging/log_level.hpp>
#include <axiom/core/logging/log_query.hpp>
#include <axiom/core/logging/log_record.hpp>
#include <axiom/core/logging/log_sink.hpp>
#include <axiom/core/logging/logger.hpp>
#include <axiom/core/logging/logging_service.hpp>
#include <axiom/core/logging/scoped_log_context.hpp>
// IWYU pragma: end_exports

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
