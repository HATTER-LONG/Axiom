#pragma once

/**
 * @file logger.hpp
 * @brief Lightweight logger bound to a LoggingService, category, and fixed fields.
 */

#include <axiom/export.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/logging/log_level.hpp>
#include <axiom/logging/scoped_log_context.hpp>

#include <format>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace axiom::logging {

namespace detail {
class LoggingState;
}

/**
 * @brief Lightweight logger bound to one service, category, and fixed fields.
 *
 * A default-constructed Logger is a safe no-op. Logger values may be copied and safely
 * carried across threads; scoped context is deliberately not carried across threads.
 */
class AXIOM_API Logger {
public:
    /** @brief Creates a no-op Logger that ignores all write operations. */
    Logger() = default;

    /**
     * @brief Creates a category beneath this Logger's category.
     * @param category Relative category segment(s) joined under this Logger's path.
     * @return Logger sharing the same service and fixed fields with an extended category.
     */
    [[nodiscard]] Logger child(std::string_view category) const;

    /**
     * @brief Creates a Logger whose fields override this Logger's fixed fields.
     * @param fields Keys that replace same-named fixed fields on the returned Logger.
     * @return Logger sharing the same service and category with merged fixed fields.
     */
    [[nodiscard]] Logger withFields(Value::Object fields) const;

    /**
     * @brief Returns whether at least one registered sink accepts this level and category.
     * @param level Candidate severity to test against registered filters.
     * @return true when a write at @p level would reach at least one sink.
     */
    [[nodiscard]] bool enabled(LogLevel level) const noexcept;

    /**
     * @brief Pushes fields into the current thread's context for this Logger's service.
     *
     * The returned guard affects every Logger created by the same LoggingService. A
     * default-constructed Logger returns an inert guard.
     *
     * @param fields Structured fields merged into subsequent events on this thread.
     * @return RAII guard that pops the context on destruction.
     */
    [[nodiscard]] ScopedLogContext scopedContext(Value::Object fields) const;

    /**
     * @brief Emits an already-formatted record without propagating logging failures.
     * @param level Severity for the event.
     * @param message Owned event message.
     * @param fields Event-local fields, which override all inherited fields.
     * @param location Call-site source location.
     * @note Sink and filter failures are swallowed so logging cannot abort business logic.
     */
    void write(LogLevel level,
               std::string message,
               Value::Object fields = {},
               std::source_location location = std::source_location::current()) const noexcept;

    /**
     * @brief Formats and writes a record, swallowing formatting failures.
     * @tparam Arguments Types of values substituted into @p format.
     * @param level Severity for the event.
     * @param fields Event-local fields, which override all inherited fields.
     * @param location Call-site source location captured by the caller or macros.
     * @param format std::format format string for the message.
     * @param arguments Values substituted into @p format.
     * @note On format failure the event is dropped and no exception escapes.
     */
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
            return;
        }
    }

private:
    friend class LoggingService;
    Logger(std::shared_ptr<detail::LoggingState> state, std::string category, Value::Object fields);

    std::shared_ptr<detail::LoggingState> state_;
    std::string category_;
    Value::Object fields_;
};

} // namespace axiom::logging

/**
 * @brief Emits a formatted log record with explicit event fields when enabled.
 *
 * Evaluates @p logger and @p level once, then calls Logger::writeFormatted only when
 * Logger::enabled returns true for that level.
 *
 * @param logger Logger expression used for the emit (may have side effects once).
 * @param level LogLevel severity for the event.
 * @param fields Value::Object of event-local fields.
 * @param ... Format string followed by std::format arguments.
 */
#define AXIOM_LOG(logger, level, fields, ...)                                                      \
    do {                                                                                           \
        auto&& axiom_log_logger_ = (logger);                                                       \
        const auto axiom_log_level_ = (level);                                                     \
        if(axiom_log_logger_.enabled(axiom_log_level_)) {                                          \
            axiom_log_logger_.writeFormatted(axiom_log_level_, (fields),                           \
                                             std::source_location::current(), __VA_ARGS__);        \
        }                                                                                          \
    } while(false)

/**
 * @brief Emits a Trace-level formatted log record with empty event fields.
 * @param logger Logger expression used for the emit.
 * @param ... Format string followed by std::format arguments.
 */
#define AXIOM_LOG_TRACE(logger, ...)                                                               \
    AXIOM_LOG((logger), ::axiom::logging::LogLevel::Trace, ::axiom::Value::Object{}, __VA_ARGS__)
/**
 * @brief Emits a Debug-level formatted log record with empty event fields.
 * @param logger Logger expression used for the emit.
 * @param ... Format string followed by std::format arguments.
 */
#define AXIOM_LOG_DEBUG(logger, ...)                                                               \
    AXIOM_LOG((logger), ::axiom::logging::LogLevel::Debug, ::axiom::Value::Object{}, __VA_ARGS__)
/**
 * @brief Emits an Info-level formatted log record with empty event fields.
 * @param logger Logger expression used for the emit.
 * @param ... Format string followed by std::format arguments.
 */
#define AXIOM_LOG_INFO(logger, ...)                                                                \
    AXIOM_LOG((logger), ::axiom::logging::LogLevel::Info, ::axiom::Value::Object{}, __VA_ARGS__)
/**
 * @brief Emits a Warning-level formatted log record with empty event fields.
 * @param logger Logger expression used for the emit.
 * @param ... Format string followed by std::format arguments.
 */
#define AXIOM_LOG_WARNING(logger, ...)                                                             \
    AXIOM_LOG((logger), ::axiom::logging::LogLevel::Warning, ::axiom::Value::Object{}, __VA_ARGS__)
/**
 * @brief Emits an Error-level formatted log record with empty event fields.
 * @param logger Logger expression used for the emit.
 * @param ... Format string followed by std::format arguments.
 */
#define AXIOM_LOG_ERROR(logger, ...)                                                               \
    AXIOM_LOG((logger), ::axiom::logging::LogLevel::Error, ::axiom::Value::Object{}, __VA_ARGS__)
/**
 * @brief Emits a Critical-level formatted log record with empty event fields.
 * @param logger Logger expression used for the emit.
 * @param ... Format string followed by std::format arguments.
 */
#define AXIOM_LOG_CRITICAL(logger, ...)                                                            \
    AXIOM_LOG((logger), ::axiom::logging::LogLevel::Critical, ::axiom::Value::Object{}, __VA_ARGS__)
