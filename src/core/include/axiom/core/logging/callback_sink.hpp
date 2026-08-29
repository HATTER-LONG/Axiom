#pragma once

/**
 * @file callback_sink.hpp
 * @brief ILogSink adapter that forwards each record to an application callback.
 */

#include <axiom/core/logging/log_record.hpp>
#include <axiom/core/logging/log_sink.hpp>

#include <functional>
#include <utility>

namespace axiom::core::logging {

/**
 * @brief Forwards each record to an application-supplied callback.
 *
 * Useful for tests and for bridging into host application observability systems.
 */
class CallbackSink final : public ILogSink {
public:
    /**
     * @brief Creates a sink that invokes @p callback for every consumed record.
     * @param callback Consumer invoked with each record; an empty callback makes
     *        consume() a no-op.
     */
    explicit CallbackSink(std::function<void(const LogRecord&)> callback)
        : callback_(std::move(callback)) {}

    /**
     * @brief Invokes the configured callback when one is present.
     * @param record Event forwarded to the callback.
     * @note A null/empty callback is silently ignored.
     */
    void consume(const LogRecord& record) override {
        if(callback_) {
            callback_(record);
        }
    }

private:
    std::function<void(const LogRecord&)> callback_;
};

} // namespace axiom::core::logging
