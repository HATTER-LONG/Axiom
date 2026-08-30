#pragma once

/**
 * @file scoped_log_context.hpp
 * @brief RAII guard that pushes thread-local structured fields for a LoggingService.
 */

#include <cstdint>
#include <memory>

namespace axiom::core::logging {

namespace detail {
class LoggingState;
}

/**
 * @brief Thread-local structured fields active for the lifetime of this object.
 *
 * A context affects only Loggers made by its associated service and must be destroyed
 * on the thread that created it. Moving transfers ownership of the push; the moved-from
 * guard becomes inert.
 *
 * @note A default-constructed guard is a no-op and is safe to destroy on any thread.
 */
class ScopedLogContext {
public:
    /** @brief Creates an inert guard that holds no context. */
    ScopedLogContext() noexcept = default;
    /** @brief Pops the associated context if this guard still owns one. */
    ~ScopedLogContext() noexcept;

    /**
     * @brief Transfers ownership of the pushed context from @p other.
     * @param other Guard that becomes inert after the move.
     */
    ScopedLogContext(ScopedLogContext&& other) noexcept;
    /**
     * @brief Replaces this guard's ownership with that of @p other.
     * @param other Guard that becomes inert after the move.
     * @return Reference to this guard.
     * @post Any context previously owned by this guard has been popped.
     */
    ScopedLogContext& operator=(ScopedLogContext&& other) noexcept;

    ScopedLogContext(const ScopedLogContext&) = delete;
    ScopedLogContext& operator=(const ScopedLogContext&) = delete;

private:
    friend class LoggingService;
    friend class Logger;
    ScopedLogContext(std::shared_ptr<detail::LoggingState> state, std::uint64_t id) noexcept;
    void reset() noexcept;

    std::shared_ptr<detail::LoggingState> state_;
    std::uint64_t id_{0};
};

} // namespace axiom::core::logging
