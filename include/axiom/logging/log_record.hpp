#pragma once

/**
 * @file log_record.hpp
 * @brief Immutable-at-dispatch structured log event delivered to sinks.
 */

#include <axiom/foundation/value.hpp>
#include <axiom/logging/log_level.hpp>

#include <chrono>
#include <cstdint>
#include <string>

namespace axiom::logging {

/**
 * @brief An immutable-at-dispatch structured log event.
 *
 * The source text is owned so a record can safely outlive the calling translation unit.
 * Field merge order at emit time is: thread-scoped context, then Logger-bound fields,
 * then event-local fields (later keys override earlier ones).
 */
struct LogRecord {
    LogLevel level{LogLevel::Info};                  ///< Severity of this event.
    std::string message;                             ///< Owned, already-formatted event message.
    std::chrono::system_clock::time_point timestamp; ///< Wall-clock time of emission.
    std::string category;                            ///< Dot-separated logger category path.
    std::string source_file;              ///< Call-site file path from std::source_location.
    std::string source_function;          ///< Call-site function name from std::source_location.
    std::uint_least32_t source_line{0};   ///< Call-site line number.
    std::uint_least32_t source_column{0}; ///< Call-site column number.
    Value::Object fields;                 ///< Merged structured fields for this event.
};

} // namespace axiom::logging
