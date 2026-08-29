#pragma once

/**
 * @file logging_service.hpp
 * @brief Owns sinks and filtering state shared by a family of Logger instances.
 */

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
    /** @brief Creates an empty subscription that owns no sink. */
    LogSubscription() noexcept = default;
    /** @brief Unregisters the sink if this subscription still owns one. */
    ~LogSubscription() noexcept;

    /**
     * @brief Transfers sink ownership from @p other.
     * @param other Subscription that becomes empty after the move.
     */
    LogSubscription(LogSubscription&& other) noexcept;
    /**
     * @brief Replaces this subscription's ownership with that of @p other.
     * @param other Subscription that becomes empty after the move.
     * @return Reference to this subscription.
     * @post Any sink previously owned by this subscription has been unregistered.
     */
    LogSubscription& operator=(LogSubscription&& other) noexcept;

    LogSubscription(const LogSubscription&) = delete;
    LogSubscription& operator=(const LogSubscription&) = delete;

    /**
     * @brief Removes the registered sink early, if it is still registered.
     * @post This subscription is empty and further reset() calls are no-ops.
     */
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
    /** @brief Creates a service with no registered sinks. */
    LoggingService();
    /** @brief Destroys the service; outstanding Loggers become no-ops after state release. */
    ~LoggingService() noexcept;

    LoggingService(LoggingService&&) noexcept = default;
    LoggingService& operator=(LoggingService&&) noexcept = default;
    LoggingService(const LoggingService&) = delete;
    LoggingService& operator=(const LoggingService&) = delete;

    /**
     * @brief Creates a lightweight Logger with the supplied category and fixed fields.
     * @param category Dot-separated category path for the returned Logger.
     * @param bound_fields Fixed fields merged into every event from that Logger.
     * @return Logger bound to this service's shared state.
     */
    [[nodiscard]] Logger logger(std::string category, Value::Object bound_fields = {}) const;

    /**
     * @brief Registers a sink and its filter; a null sink produces an empty subscription.
     * @param sink Shared ownership of the consumer; retained until the subscription ends.
     * @param filter Severity and category-prefix selection applied before consume().
     * @return Move-only subscription; destroying or resetting it removes the sink.
     * @note A null @p sink leaves the service unchanged and returns an empty subscription.
     */
    [[nodiscard]] LogSubscription addSink(std::shared_ptr<ILogSink> sink, LogFilter filter = {});

    /**
     * @brief Pushes fields for this service on the current thread's context stack.
     * @param fields Structured fields merged into subsequent events on this thread.
     * @return RAII guard that pops the context on destruction.
     */
    [[nodiscard]] ScopedLogContext scopedContext(Value::Object fields) const;

    /**
     * @brief Establishes an observability barrier for all currently registered sinks.
     *
     * Snapshots the sink list under the service lock, then invokes flush() on each
     * sink after releasing the lock. Sink failures are swallowed.
     */
    void flush() noexcept;

private:
    std::shared_ptr<detail::LoggingState> state_;
};

} // namespace axiom::core::logging
