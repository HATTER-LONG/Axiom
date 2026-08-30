#pragma once

/**
 * @file action_demo.hpp
 * @brief Typed action registration, discovery, invocation, and error walkthrough.
 */

#include <axiom/core/logging/logger.hpp>

namespace axiom::demo {

/**
 * @brief Runs action examples in a fresh local Runtime.
 * @param logger Logger for runtime events; its service must outlive this call.
 * @return true if every demonstrated invariant holds; otherwise prints a diagnostic.
 * @note Does not retain the logger or action state after returning.
 */
[[nodiscard]] bool runActionDemo(const core::logging::Logger& logger);
} // namespace axiom::demo
