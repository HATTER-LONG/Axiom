#pragma once

#include <axiom/core/logging/log_record.hpp>

namespace axiom::core::logging {

/**
 * @brief Consumer of records emitted by a LoggingService.
 *
 * @note consume and flush implementations may throw. LoggingService catches those
 * failures, continues with remaining sinks, and does not propagate them to callers.
 */
class ILogSink {
public:
    virtual ~ILogSink() = default;

    /** @brief Consumes one record. */
    virtual void consume(const LogRecord& record) = 0;
    /** @brief Makes records accepted before this call observable. */
    virtual void flush() {}
};

} // namespace axiom::core::logging
