#pragma once

#include <axiom/core/base/value.hpp>
#include <axiom/core/logging/log_level.hpp>

#include <chrono>
#include <cstdint>
#include <string>

namespace axiom::core::logging {

/**
 * @brief An immutable-at-dispatch structured log event.
 *
 * The source text is owned so a record can safely outlive the calling translation unit.
 */
struct LogRecord {
    LogLevel level{LogLevel::Info};
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    std::string category;
    std::string source_file;
    std::string source_function;
    std::uint_least32_t source_line{0};
    std::uint_least32_t source_column{0};
    Value::Object fields;
};

} // namespace axiom::core::logging
