#pragma once

/**
 * @file error.hpp
 * @brief Structured Error and ErrorCode values returned from Axiom boundaries.
 */

#include <axiom/foundation/value.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace axiom {

/**
 * @brief Stable categories for expected failures at an Axiom invocation boundary.
 */
enum class ErrorCode : std::uint8_t {
    InvalidArgument,   ///< A provided argument violates its contract.
    MissingArgument,   ///< A required argument was omitted.
    UnknownArgument,   ///< An unexpected argument name was supplied.
    TypeMismatch,      ///< An argument value does not match the declared type.
    NotFound,          ///< The requested module, action, resource, or task does not exist.
    AlreadyExists,     ///< Registration or creation collided with an existing entry.
    InvalidDescriptor, ///< A module/action/type descriptor failed validation.
    InvocationFailed,  ///< The action ran but reported a typed failure (e.g. std::exception).
    InternalError,     ///< An unexpected or non-std throwable escaped the boundary.
    Cancelled,         ///< The requested operation was cooperatively cancelled.
};

/**
 * @brief Structured failure returned from a Axiom boundary.
 *
 * When present, path identifies an input position using the public path notation
 * (for example, `points[2]` or `shape.size.x`). Details carries structured,
 * caller-safe diagnostic data.
 */
struct Error {
    /** @brief Stable failure category. */
    ErrorCode code{ErrorCode::InternalError};
    /** @brief Human-readable failure explanation. */
    std::string message;
    /** @brief Optional input path associated with the failure. */
    std::optional<std::string> path;
    /** @brief Optional structured diagnostic detail. */
    std::optional<Value> details;
};

} // namespace axiom
