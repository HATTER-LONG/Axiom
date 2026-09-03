#pragma once

/**
 * @file value_conversion.hpp
 * @brief One-to-one conversion between Python objects and Axiom Values.
 */

#include <axiom/foundation/value.hpp>

#include <pybind11/pybind11.h> // IWYU pragma: keep

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace axiom::python {

// NOLINTBEGIN(misc-include-cleaner): pybind11 types enter through SYSTEM
// headers whose symbol providers include-cleaner cannot map.
/**
 * @brief Message and public input path of one failed Python-to-Value conversion.
 *
 * Bundling the pair keeps conversion call sites free of adjacent same-typed
 * parameters that could be swapped by mistake.
 */
struct ConversionIssue {
    std::string message;
    std::string path;
};

/**
 * @brief Adapter-local failure raised when a Python object cannot map to a Value.
 *
 * ConversionError is a binding-parameter error, not a Core business failure.
 * It carries the public input path where conversion stopped.
 */
class ConversionError final : public std::runtime_error {
public:
    /**
     * @brief Creates a conversion failure from its message and public input path.
     * @param issue Conversion message paired with the failing input path.
     */
    explicit ConversionError(ConversionIssue issue)
        : std::runtime_error(issue.message), path_(std::move(issue.path)) {}

    /**
     * @brief Returns the public input path where conversion stopped.
     * @return Stable reference to the input path.
     */
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
};

/**
 * @brief Converts one Python object into an owned Value.
 *
 * None, bool, int, float, str, list, and dict map to the seven Value types.
 * bool is tested before int; int must fit int64; dict keys must be real
 * strings. Tuples, sets, bytes, Decimal, path-like or custom mapping/sequence
 * objects, and implicit __int__/__float__ conversions are rejected. Nested
 * failures carry their precise Core-style path, and recursive containers are
 * cut at a fixed depth.
 *
 * @param object Python object to convert. Must not be null.
 * @param path Public input location of object.
 * @return Owned Value; lists and dicts are freshly materialized.
 * @throws ConversionError If object is not convertible or nesting exceeds the limit.
 * @note The calling code must hold the GIL.
 */
[[nodiscard]] Value valueFromPython(const pybind11::handle& object, std::string_view path);

/**
 * @brief Converts an owned Value into a fresh Python object.
 *
 * Lists and dicts are freshly created; no Value-internal reference crosses
 * into Python.
 *
 * @param value Dynamic value to convert.
 * @return New Python object mirroring the Value.
 * @note The calling code must hold the GIL.
 */
[[nodiscard]] pybind11::object valueToPython(const Value& value);
// NOLINTEND(misc-include-cleaner)

} // namespace axiom::python