#pragma once

/**
 * @file resource_demo.hpp
 * @brief Resource registration, typed identities, and keepalive access walkthrough.
 */

namespace axiom::demo {

/**
 * @brief Runs the resource lifecycle example with a fresh registry and accumulator.
 * @return true if identity round-trip, removal, and retained access behave as expected.
 * @note Prints diagnostics on failure; all resources are released before returning.
 */
[[nodiscard]] bool runResourceDemo();
} // namespace axiom::demo
