#pragma once

#include <axiom/core/logging/log_record.hpp>

namespace axiom::core::logging {

/**
 * @brief Consumer of records emitted by a LoggingService.
 *
 * @note consume implementations may throw. LoggingService catches those failures and
 * continues dispatching the record to other registered sinks.
 */
class ILogSink {
public:
    virtual ~ILogSink() = default;

    /** @brief Consumes one record. */
    virtual void consume(const LogRecord& record) = 0;
    /** @brief Makes records accepted before this call observable. */
    virtual void flush() noexcept {}
};

} // namespace axiom::core::logging
