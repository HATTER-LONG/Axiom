#pragma once

/**
 * @file axiom.hpp
 * @brief Umbrella header for the complete public Axiom API.
 *
 * Prefer including this header from application and test code. Module-internal
 * translation units may include the narrower headers they need.
 */

// IWYU pragma: begin_exports
#include <axiom/action/action_id.hpp>
#include <axiom/action/descriptor.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/action/module.hpp>
#include <axiom/action/module_builder.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/async/executor.hpp>
#include <axiom/async/scheduler.hpp>
#include <axiom/events/signal.hpp>
#include <axiom/export.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/introspection/introspection_service.hpp>
#include <axiom/introspection/runtime_snapshot.hpp>
#include <axiom/logging/callback_sink.hpp>
#include <axiom/logging/console_sink.hpp>
#include <axiom/logging/log_collector.hpp>
#include <axiom/logging/log_filter.hpp>
#include <axiom/logging/log_level.hpp>
#include <axiom/logging/log_query.hpp>
#include <axiom/logging/log_record.hpp>
#include <axiom/logging/log_sink.hpp>
#include <axiom/logging/logger.hpp>
#include <axiom/logging/logging_service.hpp>
#include <axiom/logging/scoped_log_context.hpp>
#include <axiom/resource/handle.hpp>
#include <axiom/resource/resource_descriptor.hpp>
#include <axiom/resource/resource_id.hpp>
#include <axiom/resource/resource_ref.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/resource/resource_traits.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_registry.hpp>
#include <axiom/task/task_types.hpp>
// IWYU pragma: end_exports

#include <string_view>

namespace axiom {

/**
 * @brief Returns the framework name.
 *
 * @return A pointer to a null-terminated string with static storage duration.
 */
AXIOM_API const char* frameworkName() noexcept;

/**
 * @brief Tests whether a candidate equals the framework name.
 *
 * @param candidate Text to compare with the framework name.
 * @return `true` when the texts match; otherwise `false`.
 */
AXIOM_API bool isFrameworkName(std::string_view candidate) noexcept;

} // namespace axiom
