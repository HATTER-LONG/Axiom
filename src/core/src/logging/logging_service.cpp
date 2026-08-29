#include <axiom/core/logging/logging_service.hpp>

#include <axiom/core/base/value.hpp>
#include <axiom/core/logging/log_filter.hpp>
#include <axiom/core/logging/log_level.hpp>
#include <axiom/core/logging/log_record.hpp>
#include <axiom/core/logging/log_sink.hpp>
#include <axiom/core/logging/logger.hpp>
#include <axiom/core/logging/scoped_log_context.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace axiom::core::logging {

namespace detail {

namespace {

struct SinkRegistration {
    std::uint64_t id{0};
    std::shared_ptr<ILogSink> sink;
    LogFilter filter;
};

struct ContextEntry {
    const LoggingState* state{nullptr};
    std::uint64_t id{0};
    Value::Object fields;
};

thread_local std::vector<ContextEntry> contexts;
// Context entries are only ever compared within their owning thread-local stack.
thread_local std::uint64_t next_context_id{1};

} // namespace

class LoggingState {
public:
    [[nodiscard]] std::uint64_t addSink(std::shared_ptr<ILogSink> sink, LogFilter filter) {
        const std::scoped_lock lock{mutex_};
        const auto id = next_sink_id_++;
        sinks_.push_back({.id = id, .sink = std::move(sink), .filter = std::move(filter)});
        return id;
    }

    void removeSink(const std::uint64_t id) noexcept {
        try {
            const std::scoped_lock lock{mutex_};
            std::erase_if(sinks_, [id](const SinkRegistration& registration) {
                return registration.id == id;
            });
        } catch(...) {
            return;
        }
    }

    [[nodiscard]] bool enabled(const LogLevel level,
                               const std::string_view category) const noexcept {
        try {
            const std::scoped_lock lock{mutex_};
            return std::ranges::any_of(sinks_,
                                       [level, category](const SinkRegistration& registration) {
                                           return registration.filter.matches(level, category);
                                       });
        } catch(...) {
            return false;
        }
    }

    void dispatch(const LogRecord& record) const noexcept {
        try {
            for(const auto& sink : matchingSinks(record)) {
                try {
                    sink->consume(record);
                } catch(...) {
                    continue;
                }
            }
        } catch(...) {
            return;
        }
    }

    void flush() noexcept {
        try {
            for(const auto& sink : allSinks()) {
                try {
                    sink->flush();
                } catch(...) {
                    continue;
                }
            }
        } catch(...) {
            return;
        }
    }

private:
    [[nodiscard]] std::vector<std::shared_ptr<ILogSink>>
    matchingSinks(const LogRecord& record) const {
        const std::scoped_lock lock{mutex_};
        std::vector<std::shared_ptr<ILogSink>> recipients;
        recipients.reserve(sinks_.size());
        for(const auto& registration : sinks_) {
            if(registration.filter.matches(record.level, record.category)) {
                recipients.push_back(registration.sink);
            }
        }
        return recipients;
    }

    [[nodiscard]] std::vector<std::shared_ptr<ILogSink>> allSinks() const {
        const std::scoped_lock lock{mutex_};
        std::vector<std::shared_ptr<ILogSink>> recipients;
        recipients.reserve(sinks_.size());
        std::ranges::transform(
            sinks_, std::back_inserter(recipients),
            [](const SinkRegistration& registration) { return registration.sink; });
        return recipients;
    }

    mutable std::mutex mutex_;
    std::vector<SinkRegistration> sinks_;
    std::uint64_t next_sink_id_{1};
};

namespace {
[[nodiscard]] std::uint64_t pushContext(const std::shared_ptr<LoggingState>& state,
                                        Value::Object fields) {
    const auto id = next_context_id++;
    contexts.push_back({.state = state.get(), .id = id, .fields = std::move(fields)});
    return id;
}

void popContext(const LoggingState* const state, const std::uint64_t id) noexcept {
    for(auto index = contexts.size(); index != 0; --index) {
        const auto& entry = contexts[index - 1];
        if(entry.state == state && entry.id == id) {
            contexts.erase(contexts.begin() + static_cast<std::ptrdiff_t>(index - 1));
            return;
        }
    }
}

[[nodiscard]] Value::Object contextFields(const LoggingState* const state) {
    Value::Object fields;
    for(const auto& context : contexts) {
        if(context.state == state) {
            for(const auto& [key, value] : context.fields) {
                fields.insert_or_assign(key, value);
            }
        }
    }
    return fields;
}

} // namespace

} // namespace detail

bool LogFilter::matches(const LogLevel level, const std::string_view category) const noexcept {
    if(!isAtLeast(level, minimum_level)) {
        return false;
    }
    return category_prefixes.empty() ||
           std::ranges::any_of(category_prefixes, [category](const std::string& prefix) {
               return prefix.empty() || category == prefix ||
                      (category.starts_with(prefix) && category[prefix.size()] == '.');
           });
}

LogSubscription::LogSubscription(std::weak_ptr<detail::LoggingState> state,
                                 const std::uint64_t id) noexcept
    : state_(std::move(state)), id_(id) {}

LogSubscription::~LogSubscription() noexcept { reset(); }

LogSubscription::LogSubscription(LogSubscription&& other) noexcept
    : state_(std::move(other.state_)), id_(std::exchange(other.id_, 0)) {}

LogSubscription& LogSubscription::operator=(LogSubscription&& other) noexcept {
    if(this != &other) {
        reset();
        state_ = std::move(other.state_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

void LogSubscription::reset() noexcept {
    if(id_ != 0) {
        if(const auto state = state_.lock()) {
            state->removeSink(id_);
        }
        id_ = 0;
    }
}

ScopedLogContext::ScopedLogContext(std::shared_ptr<detail::LoggingState> state,
                                   const std::uint64_t id) noexcept
    : state_(std::move(state)), id_(id) {}

ScopedLogContext::~ScopedLogContext() noexcept { reset(); }

ScopedLogContext::ScopedLogContext(ScopedLogContext&& other) noexcept
    : state_(std::move(other.state_)), id_(std::exchange(other.id_, 0)) {}

ScopedLogContext& ScopedLogContext::operator=(ScopedLogContext&& other) noexcept {
    if(this != &other) {
        reset();
        state_ = std::move(other.state_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

void ScopedLogContext::reset() noexcept {
    if(id_ != 0) {
        detail::popContext(state_.get(), id_);
        id_ = 0;
        state_.reset();
    }
}

LoggingService::LoggingService() : state_(std::make_shared<detail::LoggingState>()) {}
LoggingService::~LoggingService() noexcept = default;

Logger LoggingService::logger(std::string category, Value::Object bound_fields) const {
    return Logger{state_, std::move(category), std::move(bound_fields)};
}

LogSubscription LoggingService::addSink(std::shared_ptr<ILogSink> sink, LogFilter filter) {
    if(!state_ || !sink) {
        return {};
    }
    return LogSubscription{state_, state_->addSink(std::move(sink), std::move(filter))};
}

ScopedLogContext LoggingService::scopedContext(Value::Object fields) const {
    if(!state_) {
        return {};
    }
    return ScopedLogContext{state_, detail::pushContext(state_, std::move(fields))};
}

void LoggingService::flush() noexcept {
    if(state_) {
        state_->flush();
    }
}

Logger::Logger(std::shared_ptr<detail::LoggingState> state,
               std::string category,
               Value::Object fields)
    : state_(std::move(state)), category_(std::move(category)), fields_(std::move(fields)) {}

Logger Logger::child(const std::string_view category) const {
    if(category_.empty()) {
        return Logger{state_, std::string{category}, fields_};
    }
    if(category.empty()) {
        return *this;
    }
    auto child_category = category_;
    child_category.append(".").append(category);
    return Logger{state_, std::move(child_category), fields_};
}

Logger Logger::withFields(Value::Object fields) const {
    auto merged = fields_;
    for(auto& [key, value] : fields) {
        merged.insert_or_assign(key, std::move(value));
    }
    return Logger{state_, category_, std::move(merged)};
}

bool Logger::enabled(const LogLevel level) const noexcept {
    return state_ && state_->enabled(level, category_);
}

ScopedLogContext Logger::scopedContext(Value::Object fields) const {
    if(!state_) {
        return {};
    }
    return ScopedLogContext{state_, detail::pushContext(state_, std::move(fields))};
}

void Logger::write(const LogLevel level,
                   std::string message,
                   Value::Object fields,
                   const std::source_location location) const noexcept {
    if(!state_) {
        return;
    }
    try {
        auto all_fields = detail::contextFields(state_.get());
        for(const auto& [key, value] : fields_) {
            all_fields.insert_or_assign(key, value);
        }
        for(auto& [key, value] : fields) {
            all_fields.insert_or_assign(key, std::move(value));
        }
        state_->dispatch({.level = level,
                          .message = std::move(message),
                          .timestamp = std::chrono::system_clock::now(),
                          .category = category_,
                          .source_file = location.file_name(),
                          .source_function = location.function_name(),
                          .source_line = location.line(),
                          .source_column = location.column(),
                          .fields = std::move(all_fields)});
    } catch(...) {
        return;
    }
}

} // namespace axiom::core::logging
