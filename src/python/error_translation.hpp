#pragma once

/**
 * @file error_translation.hpp
 * @brief Deterministic mapping from Core and adapter failures to Python exceptions.
 */

#include <axiom/foundation/error.hpp>

#include "value_conversion.hpp"

#include <pybind11/pybind11.h> // IWYU pragma: keep
#include <pybind11/pytypes.h>

#include <string_view>

namespace axiom::python {

/**
 * @brief Interpreter-owned exception types for one loaded _axiom module.
 *
 * Instances are
 * retained only by Python objects owned by the module or a
 * Python Host. Keeping them out of C++
 * static storage makes interpreter
 * finalization and a later interpreter initialization
 * independent.
 */
struct ErrorTypes final {
    pybind11::object axiom;
    pybind11::object host_closed;
    pybind11::object conversion;
};

/**
 * @brief Creates the adapter exception hierarchy on _axiom.
 *
 * Registers AxiomError (Exception), AxiomHostClosedError (AxiomError), and
 * AxiomConversionError (ValueError) as module attributes so the Python facade
 * can re-export the same authoritative types.
 *
 * @param module _axiom module receiving the exception attributes.
 */
[[nodiscard]] ErrorTypes initializeErrors(pybind11::module_& module);

/**
 * @brief Raises AxiomError carrying all four Error fields.
 * @param error Structured Core failure; code uses the stable lowercase name.
 */
[[noreturn]] void raiseAxiomError(const ErrorTypes& types, const Error& error);

/**
 * @brief Raises AxiomHostClosedError with the host_closed code.
 * @param message Human-readable closure explanation.
 */
[[noreturn]] void raiseHostClosed(const ErrorTypes& types, std::string_view message);

/**
 * @brief Raises AxiomConversionError with the failing input path.
 * @param error Conversion failure carrying the message and public input path.
 */
[[noreturn]] void raiseConversionError(const ErrorTypes& types, const ConversionError& error);

/** @brief Raises MemoryError for allocation failures crossing the boundary. */
[[noreturn]] void raiseMemoryError();

/** @brief Raises a sanitized RuntimeError for unexpected adapter failures. */
[[noreturn]] void raiseInternalAdapterError();

} // namespace axiom::python
