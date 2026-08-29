#pragma once

#include <axiom/core/base/value.hpp>
#include <axiom/core/logging/log_level.hpp>
#include <axiom/core/logging/scoped_log_context.hpp>

#include <format>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace axiom::core::logging {

namespace detail {
class LoggingState;
}

/**
 * @brief Lightweight logger bound to one service, category, and fixed fields.
 *
 * A default-constructed Logger is a safe no-op. Logger values may be copied and safely
 * carried across threads; scoped context is deliberately not carried across threads.
 */
class Logger {
public:
    Logger() noexcept = default;

    /** @brief Creates a category beneath this Logger's category. */
    [[nodiscard]] Logger child(std::string_view category) const;
    /** @brief Creates a Logger whose fields override this Logger's fixed fields. */
    [[nodiscard]] Logger withFields(Value::Object fields) const;
    /** @brief Returns whether at least one registered sink accepts this level and category. */
    [[nodiscard]] bool enabled(LogLevel level) const noexcept;
    /**
     * @brief Pushes fields into the current thread's context for this Logger's service.
     *
     * The returned guard affects every Logger created by the same LoggingService. A
     * default-constructed Logger returns an inert guard.
     */
    [[nodiscard]] ScopedLogContext scopedContext(Value::Object fields) const;

    /**
     * @brief Emits an already-formatted record without propagating logging failures.
     * @param level Severity for the event.
     * @param message Owned event message.
     * @param fields Event-local fields, which override all inherited fields.
     * @param location Call-site source location.
     */
    void write(LogLevel level,
               std::string message,
               Value::Object fields = {},
               std::source_location location = std::source_location::current()) const noexcept;

    /** @brief Formats and writes a record, swallowing formatting failures. */
    template <typename... Arguments>
    void writeFormatted(LogLevel level,
                        Value::Object fields,
                        std::source_location location,
                        std::format_string<Arguments...> format,
                        Arguments&&... arguments) const noexcept {
        try {
            write(level, std::format(format, std::forward<Arguments>(arguments)...),
                  std::move(fields), location);
        } catch(...) {
        }
    }

private:
    friend class LoggingService;
    Logger(std::shared_ptr<detail::LoggingState> state, std::string category, Value::Object fields);

    std::shared_ptr<detail::LoggingState> state_;
    std::string category_;
    Value::Object fields_;
};

} // namespace axiom::core::logging

/** @brief Emits a formatted log record with explicit event fields. */
#define AXIOM_LOG(logger, level, fields, ...)                                                      \
    do {                                                                                           \
        auto&& axiom_log_logger_ = (logger);                                                       \
        const auto axiom_log_level_ = (level);                                                     \
        if(axiom_log_logger_.enabled(axiom_log_level_)) {                                          \
            axiom_log_logger_.writeFormatted(axiom_log_level_, (fields),                           \
                                             std::source_location::current(), __VA_ARGS__);        \
        }                                                                                          \
    } while(false)

/** @brief Emits a Trace-level formatted log record. */
#define AXIOM_LOG_TRACE(logger, ...)                                                               \
    AXIOM_LOG((logger), ::axiom::core::logging::LogLevel::Trace, ::axiom::core::Value::Object{},   \
              __VA_ARGS__)
/** @brief Emits a Debug-level formatted log record. */
#define AXIOM_LOG_DEBUG(logger, ...)                                                               \
    AXIOM_LOG((logger), ::axiom::core::logging::LogLevel::Debug, ::axiom::core::Value::Object{},   \
              __VA_ARGS__)
/** @brief Emits an Info-level formatted log record. */
#define AXIOM_LOG_INFO(logger, ...)                                                                \
    AXIOM_LOG((logger), ::axiom::core::logging::LogLevel::Info, ::axiom::core::Value::Object{},    \
              __VA_ARGS__)
/** @brief Emits a Warning-level formatted log record. */
#define AXIOM_LOG_WARNING(logger, ...)                                                             \
    AXIOM_LOG((logger), ::axiom::core::logging::LogLevel::Warning, ::axiom::core::Value::Object{}, \
              __VA_ARGS__)
/** @brief Emits an Error-level formatted log record. */
#define AXIOM_LOG_ERROR(logger, ...)                                                               \
    AXIOM_LOG((logger), ::axiom::core::logging::LogLevel::Error, ::axiom::core::Value::Object{},   \
              __VA_ARGS__)
/** @brief Emits a Critical-level formatted log record. */
#define AXIOM_LOG_CRITICAL(logger, ...)                                                            \
    AXIOM_LOG((logger), ::axiom::core::logging::LogLevel::Critical,                                \
              ::axiom::core::Value::Object{}, __VA_ARGS__)
