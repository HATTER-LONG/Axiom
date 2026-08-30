#pragma once

/**
 * @file logging_demo.hpp
 * @brief Owns walkthrough logging, including sink subscriptions and collected events.
 */

#include <axiom/core/logging/log_collector.hpp>
#include <axiom/core/logging/logger.hpp>
#include <axiom/core/logging/logging_service.hpp>

#include <cstddef>
#include <memory>

namespace axiom::demo {

/**
 * @brief A non-copyable logging session for one walkthrough.
 *
 * Owns sinks, callback state, and subscriptions; callers only obtain the runtime
 * logger and run the logging example. Use from one thread. Destruction flushes
 * before subscriptions and sinks are released, including on early failure.
 */
class LoggingDemo {
public:
    /** @brief Installs console, collector, and callback sinks and logs session start. */
    LoggingDemo();
    /** @brief Flushes events before releasing subscriptions and the service. */
    ~LoggingDemo();

    LoggingDemo(const LoggingDemo&) = delete;
    LoggingDemo& operator=(const LoggingDemo&) = delete;
    LoggingDemo(LoggingDemo&&) = delete;
    LoggingDemo& operator=(LoggingDemo&&) = delete;

    /**
     * @brief Returns the logger used by the action example.
     * @return Logger that becomes a no-op after this session is destroyed.
     */
    [[nodiscard]] core::logging::Logger runtimeLogger() const;

    /**
     * @brief Demonstrates structured logging and summarizes collected runtime events.
     * @return true if logging is enabled and runtime events have been collected.
     * @note Run the action example with runtimeLogger() first to populate the summary.
     */
    [[nodiscard]] bool run();

private:
    // Reverse destruction releases subscriptions before their state and service.
    core::logging::LoggingService service_;
    std::shared_ptr<core::logging::LogCollector> collector_;
    std::shared_ptr<std::size_t> callback_count_;
    core::logging::LogSubscription console_;
    core::logging::LogSubscription memory_;
    core::logging::LogSubscription callback_;
};
} // namespace axiom::demo
