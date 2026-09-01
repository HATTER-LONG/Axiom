#include <axiom/logging/console_sink.hpp>
#include <axiom/logging/log_level.hpp>
#include <axiom/logging/log_record.hpp>

#include <axiom/foundation/value.hpp>

#include <spdlog/common.h>
#include <spdlog/details/os.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ansicolor_sink.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <format>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace axiom::logging {

namespace {

[[nodiscard]] const char* levelName(const LogLevel level) noexcept {
    switch(level) {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warning";
    case LogLevel::Error:
        return "error";
    case LogLevel::Critical:
        return "critical";
    }
    return "unknown";
}

[[nodiscard]] spdlog::level::level_enum spdlogLevel(const LogLevel level) noexcept {
    switch(level) {
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warning:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    case LogLevel::Critical:
        return spdlog::level::critical;
    }
    return spdlog::level::off;
}

void appendEscaped(std::string& text, const std::string& value) {
    text.push_back('"');
    for(const char character : value) {
        if(character == '"' || character == '\\') {
            text.push_back('\\');
        }
        text.push_back(character);
    }
    text.push_back('"');
}

// Iterative Value renderer: children are pushed in reverse so a LIFO walk emits
// nested arrays/objects in declaration order without recursion (avoids deep Value trees
// blowing the call stack).
struct RenderTask {
    enum class Kind : std::uint8_t { Value, Text, Key };

    Kind kind{Kind::Value};
    const Value* value{nullptr};
    const std::string* key{nullptr};
    std::string_view text;
};

void scheduleArray(std::string& text, std::vector<RenderTask>& tasks, const Value::Array& values) {
    text.push_back('[');
    // Closing bracket is scheduled first so it is emitted after all elements.
    tasks.push_back({.kind = RenderTask::Kind::Text, .text = "]"});
    bool is_last = true;
    for(const auto& item : std::views::reverse(values)) {
        if(is_last) {
            is_last = false;
        } else {
            tasks.push_back({.kind = RenderTask::Kind::Text, .text = ", "});
        }
        tasks.push_back({.kind = RenderTask::Kind::Value, .value = &item, .text = {}});
    }
}

void scheduleObject(std::string& text,
                    std::vector<RenderTask>& tasks,
                    const Value::Object& fields) {
    text.push_back('{');
    tasks.push_back({.kind = RenderTask::Kind::Text, .text = "}"});
    bool is_last = true;
    // Reverse walk + push Key then Value so pop order is Key, Value, separator...
    for(const auto& item : std::views::reverse(fields)) {
        if(is_last) {
            is_last = false;
        } else {
            tasks.push_back({.kind = RenderTask::Kind::Text, .text = ", "});
        }
        tasks.push_back({.kind = RenderTask::Kind::Value, .value = &item.second, .text = {}});
        tasks.push_back({.kind = RenderTask::Kind::Key, .key = &item.first, .text = {}});
    }
}

void appendScalar(std::string& text, const Value& value) {
    switch(value.type()) {
    case Value::Type::Null:
        text.append("null");
        break;
    case Value::Type::Boolean:
        text.append(value.asBoolean() ? "true" : "false");
        break;
    case Value::Type::Integer:
        text.append(std::to_string(value.asInteger()));
        break;
    case Value::Type::Number:
        text.append(std::to_string(value.asNumber()));
        break;
    case Value::Type::String:
        appendEscaped(text, value.asString());
        break;
    case Value::Type::Array:
    case Value::Type::Object:
        break;
    }
}

void appendTask(std::string& text, std::vector<RenderTask>& tasks, const RenderTask& task) {
    if(task.kind == RenderTask::Kind::Text) {
        text.append(task.text);
        return;
    }
    if(task.kind == RenderTask::Kind::Key) {
        text.append(*task.key).append("=");
        return;
    }
    if(task.value->isArray()) {
        scheduleArray(text, tasks, task.value->asArray());
        return;
    }
    if(task.value->isObject()) {
        scheduleObject(text, tasks, task.value->asObject());
        return;
    }
    appendScalar(text, *task.value);
}

void appendValue(std::string& text, const Value& value) {
    std::vector<RenderTask> tasks{{.kind = RenderTask::Kind::Value, .value = &value, .text = {}}};
    // Explicit stack replaces recursion; nested containers push more work onto tasks.
    while(!tasks.empty()) {
        const auto task = tasks.back();
        tasks.pop_back();
        appendTask(text, tasks, task);
    }
}

[[nodiscard]] std::string utcTime(const std::chrono::system_clock::time_point timestamp) {
    const auto rounded = std::chrono::floor<std::chrono::milliseconds>(timestamp);
    const auto seconds = std::chrono::floor<std::chrono::seconds>(rounded);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(rounded - seconds);
    const auto raw_time = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc;
#ifdef _WIN32
    gmtime_s(&utc, &raw_time);
#else
    gmtime_r(&raw_time, &utc);
#endif
    std::array<char, 32> date;
    static_cast<void>(std::strftime(date.data(), date.size(), "%Y-%m-%dT%H:%M:%S", &utc));
    return std::format("{}.{:03}Z", date.data(), milliseconds.count());
}

[[nodiscard]] bool isDisplayedCorrelationField(const std::string_view key,
                                               const Value& value) noexcept {
    return value.isString() &&
           (key == "request_id" || key == "trace_id" || key == "action" || key == "task_id");
}

void appendCorrelationFields(std::string& text, const Value::Object& fields) {
    constexpr std::array correlation_fields{
        std::pair{"request_id", "req"},
        std::pair{"trace_id", "trace"},
        std::pair{"action", "action"},
        std::pair{"task_id", "task"},
    };

    bool has_fields = false;
    for(const auto& [key, label] : correlation_fields) {
        const auto field = fields.find(key);
        if(field == fields.end() || !field->second.isString()) {
            continue;
        }
        if(!has_fields) {
            text.append(" [");
            has_fields = true;
        } else {
            text.push_back(' ');
        }
        text.append(label).push_back(':');
        text.append(field->second.asString());
    }
    if(has_fields) {
        text.push_back(']');
    }
}

[[nodiscard]] std::string formatRecord(const LogRecord& record) {
    auto text =
        std::format("[{}] [{}|{}] [tid:{}] {}", utcTime(record.timestamp), record.category,
                    levelName(record.level), spdlog::details::os::thread_id(), record.message);
    appendCorrelationFields(text, record.fields);

    Value::Object details;
    for(const auto& [key, value] : record.fields) {
        if(!isDisplayedCorrelationField(key, value)) {
            details.insert_or_assign(key, value);
        }
    }
    if(!details.empty()) {
        text.append(" ");
        appendValue(text, Value{std::move(details)});
    }
    return text;
}

} // namespace

class ConsoleSink::Implementation {
public:
    Implementation()
        : logger{std::make_shared<spdlog::logger>(
              "axiom.console",
              std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>(
                  spdlog::color_mode::always))} {
        logger->set_pattern("%^%v%$");
        logger->set_level(spdlog::level::trace);
    }

    std::shared_ptr<spdlog::logger> logger;
};

ConsoleSink::ConsoleSink() : implementation_(std::make_unique<Implementation>()) {}
ConsoleSink::~ConsoleSink() noexcept = default;
ConsoleSink::ConsoleSink(ConsoleSink&&) noexcept = default;
ConsoleSink& ConsoleSink::operator=(ConsoleSink&&) noexcept = default;

void ConsoleSink::consume(const LogRecord& record) {
    if(implementation_ == nullptr) {
        return;
    }
    implementation_->logger->log(spdlogLevel(record.level), formatRecord(record));
}

void ConsoleSink::flush() noexcept {
    if(implementation_ == nullptr) {
        return;
    }
    try {
        implementation_->logger->flush();
    } catch(...) {
        return;
    }
}

} // namespace axiom::logging
