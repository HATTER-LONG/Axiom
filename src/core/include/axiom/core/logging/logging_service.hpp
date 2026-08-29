#pragma once

#include <axiom/core/base/value.hpp>
#include <axiom/core/logging/log_filter.hpp>
#include <axiom/core/logging/log_sink.hpp>
#include <axiom/core/logging/logger.hpp>
#include <axiom/core/logging/scoped_log_context.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace axiom::core::logging {

namespace detail {
class LoggingState;
}

/**
 * @brief Move-only registration that removes its sink at destruction.
 *
 * An empty subscription has no effect and is useful when a caller elects not to
 * register a sink.
 */
class LogSubscription {
public:
    LogSubscription() noexcept = default;
    ~LogSubscription() noexcept;

    LogSubscription(LogSubscription&& other) noexcept;
    LogSubscription& operator=(LogSubscription&& other) noexcept;
    LogSubscription(const LogSubscription&) = delete;
    LogSubscription& operator=(const LogSubscription&) = delete;

    /** @brief Removes the registered sink early, if it is still registered. */
    void reset() noexcept;

private:
    friend class LoggingService;
    LogSubscription(std::weak_ptr<detail::LoggingState> state, std::uint64_t id) noexcept;

    std::weak_ptr<detail::LoggingState> state_;
    std::uint64_t id_{0};
};

/**
 * @brief Owns the sinks and filtering state shared by a family of Logger instances.
 *
 * Dispatch snapshots matching sinks under a lock and calls them after releasing it,
 * allowing sinks to safely log recursively. The current implementation dispatches
 * synchronously, but callers must use flush() when they need an observability barrier.
 */
class LoggingService {
public:
    LoggingService();
    ~LoggingService() noexcept;

    LoggingService(LoggingService&&) noexcept = default;
    LoggingService& operator=(LoggingService&&) noexcept = default;
    LoggingService(const LoggingService&) = delete;
    LoggingService& operator=(const LoggingService&) = delete;

    /** @brief Creates a lightweight Logger with the supplied category and fixed fields. */
    [[nodiscard]] Logger logger(std::string category, Value::Object bound_fields = {}) const;
    /** @brief Registers a sink and its filter; a null sink produces an empty subscription. */
    [[nodiscard]] LogSubscription addSink(std::shared_ptr<ILogSink> sink, LogFilter filter = {});
    /** @brief Pushes fields for this service on the current thread's context stack. */
    [[nodiscard]] ScopedLogContext scopedContext(Value::Object fields) const;
    /** @brief Establishes an observability barrier for all currently registered sinks. */
    void flush() noexcept;

private:
    std::shared_ptr<detail::LoggingState> state_;
};

} // namespace axiom::core::logging
