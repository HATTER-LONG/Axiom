#pragma once

/**
 * @file base_demo.hpp
 * @brief Framework identity and dynamic Value walkthrough.
 */

namespace axiom::demo {

/**
 * @brief Prints and checks the framework identity and a JSON-shaped Value.
 * @return true if every demonstrated invariant holds; otherwise prints a diagnostic.
 */
[[nodiscard]] bool runBaseDemo();
} // namespace axiom::demo
