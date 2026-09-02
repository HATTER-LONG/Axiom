#pragma once

/**
 * @file command_methods.hpp
 * @brief Stable CommandDispatcher method names.
 */

#include <string_view>

namespace axiom::command {

/** @brief Lists Actions matching optional module and tag filters. */
inline constexpr std::string_view action_list = "action.list";
/** @brief Returns one Action descriptor by canonical identifier. */
inline constexpr std::string_view action_describe = "action.describe";
/** @brief Invokes a registered Action with a dynamic argument object. */
inline constexpr std::string_view action_invoke = "action.invoke";
/** @brief Lists Resources matching an optional type filter. */
inline constexpr std::string_view resource_list = "resource.list";
/** @brief Returns one Resource descriptor by canonical identifier. */
inline constexpr std::string_view resource_describe = "resource.describe";
/** @brief Lists Tasks matching optional state and origin filters. */
inline constexpr std::string_view task_list = "task.list";
/** @brief Returns one Task descriptor by canonical identifier. */
inline constexpr std::string_view task_describe = "task.describe";
/** @brief Requests cancellation for a known Task. */
inline constexpr std::string_view task_cancel = "task.cancel";
/** @brief Collects a sequential, non-atomic snapshot of discoverable objects. */
inline constexpr std::string_view system_snapshot = "system.snapshot";

} // namespace axiom::command
