#pragma once

/**
 * @file demo_output.hpp
 * @brief Console presentation shared by the walkthrough examples.
 */

#include <axiom/core/base/error.hpp>

#include <string_view>

namespace axiom::demo {

/** @brief Prints a string view without requiring a null terminator.
 * @param text Text to write to stdout; not retained.
 */
void printView(std::string_view text);

/** @brief Prints a structured error and its optional argument path.
 * @param error Failure to display on stdout; not retained.
 */
void printError(const core::Error& error);
} // namespace axiom::demo
