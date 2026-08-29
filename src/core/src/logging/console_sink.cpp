#include <axiom/core/logging/console_sink.hpp>

#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace axiom::core::logging {

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

void appendValue(std::string& text, const Value& value);

void appendArray(std::string& text, const Value::Array& values) {
    text.push_back('[');
    bool first = true;
    for(const auto& value : values) {
        if(!std::exchange(first, false)) {
            text.append(", ");
        }
        appendValue(text, value);
    }
    text.push_back(']');
}

void appendObject(std::string& text, const Value::Object& fields) {
    text.push_back('{');
    bool first = true;
    for(const auto& [key, value] : fields) {
        if(!std::exchange(first, false)) {
            text.append(", ");
        }
        text.append(key).append("=");
        appendValue(text, value);
    }
    text.push_back('}');
}

void appendValue(std::string& text, const Value& value) {
    switch(value.type()) {
    case Value::Type::Null:
        text.append("null");
        return;
    case Value::Type::Boolean:
        text.append(value.asBoolean() ? "true" : "false");
        return;
    case Value::Type::Integer:
        text.append(std::to_string(value.asInteger()));
        return;
    case Value::Type::Number:
        text.append(std::to_string(value.asNumber()));
        return;
    case Value::Type::String:
        appendEscaped(text, value.asString());
        return;
    case Value::Type::Array:
        appendArray(text, value.asArray());
        return;
    case Value::Type::Object:
        appendObject(text, value.asObject());
        return;
    }
}

[[nodiscard]] std::string utcTime(const std::chrono::system_clock::time_point timestamp) {
    const auto rounded = std::chrono::floor<std::chrono::milliseconds>(timestamp);
    const auto seconds = std::chrono::floor<std::chrono::seconds>(rounded);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(rounded - seconds);
    const auto raw_time = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &raw_time);
#else
    gmtime_r(&raw_time, &utc);
#endif
    char date[32]{};
    static_cast<void>(std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &utc));
    return std::format("{}.{:03}Z", date, milliseconds.count());
}

[[nodiscard]] std::string formatRecord(const LogRecord& record) {
    auto text = std::format("{} [{}] [{}] {} ({}:{} {})", utcTime(record.timestamp),
                            levelName(record.level), record.category, record.message,
                            record.source_file, record.source_line, record.source_function);
    if(!record.fields.empty()) {
        text.append(" ");
        appendValue(text, Value{record.fields});
    }
    return text;
}

} // namespace

class ConsoleSink::Implementation {
public:
    Implementation() {
        const auto sink =
            std::make_shared<spdlog::sinks::stderr_color_sink_mt>(spdlog::color_mode::always);
        logger = std::make_shared<spdlog::logger>("axiom.console", std::move(sink));
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
    implementation_->logger->log(spdlogLevel(record.level), formatRecord(record));
}

void ConsoleSink::flush() noexcept {
    try {
        implementation_->logger->flush();
    } catch(...) {
    }
}

} // namespace axiom::core::logging
