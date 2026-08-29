#pragma once

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
 * on the thread that created it.
 */
class ScopedLogContext {
public:
    ScopedLogContext() noexcept = default;
    ~ScopedLogContext() noexcept;

    ScopedLogContext(ScopedLogContext&& other) noexcept;
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
