#pragma once

#include <axiom/core/base/value.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace axiom::core {

/** @brief Stable categories for expected failures at a Core invocation boundary. */
enum class ErrorCode : std::uint8_t {
    InvalidArgument,
    MissingArgument,
    UnknownArgument,
    TypeMismatch,
    NotFound,
    AlreadyExists,
    InvalidDescriptor,
    InvocationFailed,
    InternalError,
};

/**
 * @brief Structured failure returned from a Core boundary.
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

} // namespace axiom::core
