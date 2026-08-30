#pragma once

/**
 * @file log_sink.hpp
 * @brief Sink interface that consumes LogRecord values from a LoggingService.
 */

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

    /**
     * @brief Consumes one record.
     * @param record Event to observe; valid only for the duration of the call unless
     *        the sink copies it.
     * @throws Implementation-defined exceptions; LoggingService swallows them.
     */
    virtual void consume(const LogRecord& record) = 0;

    /**
     * @brief Makes records accepted before this call observable to external observers.
     *
     * Default implementation is a no-op. Synchronous sinks may leave this empty;
     * buffered or asynchronous sinks must flush pending work.
     *
     * @throws Implementation-defined exceptions; LoggingService swallows them.
     */
    virtual void flush() {}
};

} // namespace axiom::core::logging
