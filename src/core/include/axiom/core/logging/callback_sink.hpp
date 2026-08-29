#pragma once

#include <axiom/core/logging/log_sink.hpp>

#include <functional>
#include <utility>

namespace axiom::core::logging {

/** @brief Forwards each record to an application-supplied callback. */
class CallbackSink final : public ILogSink {
public:
    /** @brief Creates a sink that invokes @p callback for every consumed record. */
    explicit CallbackSink(std::function<void(const LogRecord&)> callback)
        : callback_(std::move(callback)) {}

    /** @brief Invokes the configured callback when one is present. */
    void consume(const LogRecord& record) override {
        if(callback_) {
            callback_(record);
        }
    }

private:
    std::function<void(const LogRecord&)> callback_;
};

} // namespace axiom::core::logging
