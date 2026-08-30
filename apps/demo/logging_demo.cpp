#include "logging_demo.hpp"

#include <axiom/core/core.hpp>

#include <cstddef>
#include <cstdio>
#include <memory>

namespace axiom::demo {
namespace {

using axiom::core::logging::CallbackSink;
using axiom::core::logging::ConsoleSink;
using axiom::core::logging::LogCollector;
using axiom::core::logging::LogFilter;
using axiom::core::logging::Logger;
using axiom::core::logging::LogLevel;
using axiom::core::logging::LogQuery;
using axiom::core::logging::LogRecord;
using core::Value;

[[nodiscard]] const char* logLevelName(const LogLevel level) noexcept {
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

bool showApplicationLogging(const Logger& app) {
    std::puts("\nApplication logging (child, fields, scoped context):");
    const auto session = app.child("session").withFields({{"component", Value{"walkthrough"}}});
    AXIOM_LOG_INFO(session, "child logger category is demo.session");
    const Value::Object event_fields{{"step", Value{"fields"}}};
    AXIOM_LOG(session, LogLevel::Warning, event_fields, "event fields override logger-bound keys");
    {
        const auto scoped = app.scopedContext({{"phase", Value{"scoped"}}});
        AXIOM_LOG_DEBUG(app, "debug records reach LogCollector, not the Info console sink");
    }
    if(!app.enabled(LogLevel::Info)) {
        std::puts("  expected Info logging to be enabled");
        return false;
    }
    std::puts("  emitted Info/Warning to stderr; Debug retained in memory");
    return true;
}

bool summarizeLogs(const LogCollector& collector, const std::size_t callback_count) {
    const auto runtime_logs = collector.query(
        LogQuery{.minimum_level = LogLevel::Debug, .category_prefixes = {"runtime"}, .limit = 8});
    std::puts("\nLogCollector query (runtime.*, newest 8):");
    for(const auto& record : runtime_logs) {
        std::printf("  [%s] %s %s\n", logLevelName(record.level), record.category.c_str(),
                    record.message.c_str());
    }
    std::printf("CallbackSink observed %zu records\n", callback_count);
    return !runtime_logs.empty() && callback_count > 0U;
}

} // namespace

LoggingDemo::LoggingDemo()
    : collector_(std::make_shared<LogCollector>(128)),
      callback_count_(std::make_shared<std::size_t>(0)) {
    console_ =
        service_.addSink(std::make_shared<ConsoleSink>(),
                         LogFilter{.minimum_level = LogLevel::Info, .category_prefixes = {}});
    memory_ = service_.addSink(collector_);
    callback_ = service_.addSink(
        std::make_shared<CallbackSink>([count = callback_count_](const LogRecord&) { ++*count; }));
    const auto app = service_.logger("demo", {{"session", Value{"walkthrough"}}});
    AXIOM_LOG_INFO(app, "starting core walkthrough");
}

LoggingDemo::~LoggingDemo() { service_.flush(); }

core::logging::Logger LoggingDemo::runtimeLogger() const { return service_.logger("runtime"); }

bool LoggingDemo::run() {
    const auto app = service_.logger("demo", {{"session", Value{"walkthrough"}}});
    const auto success = showApplicationLogging(app);
    service_.flush();
    return success && summarizeLogs(*collector_, *callback_count_);
}

} // namespace axiom::demo
