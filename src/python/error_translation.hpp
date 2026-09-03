#pragma once

/**
 * @file error_translation.hpp
 * @brief Deterministic mapping from Core and adapter failures to Python exceptions.
 */

#include <axiom/foundation/error.hpp>

#include "value_conversion.hpp"

#include <pybind11/pybind11.h> // IWYU pragma: keep

#include <string_view>

namespace axiom::python {

/**
 * @brief Creates the adapter exception hierarchy on _axiom.
 *
 * Registers AxiomError (Exception), AxiomHostClosedError (AxiomError), and
 * AxiomConversionError (ValueError) as module attributes so the Python facade
 * can re-export the same authoritative types.
 *
 * @param module _axiom module receiving the exception attributes.
 */
void initializeErrors(pybind11::module_& module);

/**
 * @brief Raises AxiomError carrying all four Error fields.
 * @param error Structured Core failure; code uses the stable lowercase name.
 */
[[noreturn]] void raiseAxiomError(const Error& error);

/**
 * @brief Raises AxiomHostClosedError with the host_closed code.
 * @param message Human-readable closure explanation.
 */
[[noreturn]] void raiseHostClosed(std::string_view message);

/**
 * @brief Raises AxiomConversionError with the failing input path.
 * @param error Conversion failure carrying the message and public input path.
 */
[[noreturn]] void raiseConversionError(const ConversionError& error);

/** @brief Raises MemoryError for allocation failures crossing the boundary. */
[[noreturn]] void raiseMemoryError();

/** @brief Raises a sanitized RuntimeError for unexpected adapter failures. */
[[noreturn]] void raiseInternalAdapterError();

} // namespace axiom::python