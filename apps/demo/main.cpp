#include <axiom/core/core.hpp>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

using axiom::core::ActionDescriptor;
using axiom::core::ActionId;
using axiom::core::Arguments;
using axiom::core::Error;
using axiom::core::ErrorCode;
using axiom::core::InvocationContext;
using axiom::core::ModuleBuilder;
using axiom::core::ModuleDescriptor;
using axiom::core::param;
using axiom::core::Result;
using axiom::core::Runtime;
using axiom::core::TypeDescriptor;
using axiom::core::Value;
using axiom::core::logging::CallbackSink;
using axiom::core::logging::ConsoleSink;
using axiom::core::logging::LogCollector;
using axiom::core::logging::LogFilter;
using axiom::core::logging::Logger;
using axiom::core::logging::LoggingService;
using axiom::core::logging::LogLevel;
using axiom::core::logging::LogQuery;
using axiom::core::logging::LogRecord;
using axiom::core::logging::LogSubscription;

[[nodiscard]] InvocationContext demoContext() {
    return {.request_id = "demo-1",
            .trace_id = "trace-walkthrough",
            .caller = "axiom_demo",
            .metadata = {{"surface", "cli"}}};
}

class Accumulator {
public:
    [[nodiscard]] int add(const int delta) {
        total_ += delta;
        return total_;
    }

private:
    int total_{0};
};

struct DemoLogging {
    LoggingService service;
    std::shared_ptr<LogCollector> collector = std::make_shared<LogCollector>(128);
    std::shared_ptr<std::size_t> callback_count = std::make_shared<std::size_t>(0);
    LogSubscription console;
    LogSubscription memory;
    LogSubscription callback;
};

[[nodiscard]] const char* typeKindName(const TypeDescriptor::Kind kind) noexcept {
    switch(kind) {
    case TypeDescriptor::Kind::Null:
        return "null";
    case TypeDescriptor::Kind::Boolean:
        return "boolean";
    case TypeDescriptor::Kind::Integer:
        return "integer";
    case TypeDescriptor::Kind::Number:
        return "number";
    case TypeDescriptor::Kind::String:
        return "string";
    case TypeDescriptor::Kind::Array:
        return "array";
    case TypeDescriptor::Kind::Object:
        return "object";
    }
    return "unknown";
}

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

void printView(const std::string_view text) {
    std::printf("%.*s", static_cast<int>(text.size()), text.data());
}

void printError(const Error& error) {
    std::printf("  -> %s", error.message.c_str());
    if(error.path) {
        std::printf(" (at %s)", error.path->c_str());
    }
    std::printf("\n");
}

void installSinks(DemoLogging& logging) {
    logging.console = logging.service.addSink(std::make_shared<ConsoleSink>(),
                                              LogFilter{.minimum_level = LogLevel::Info});
    logging.memory = logging.service.addSink(logging.collector);
    logging.callback = logging.service.addSink(std::make_shared<CallbackSink>(
        [count = logging.callback_count](const LogRecord&) { ++*count; }));
}

axiom::core::Result<double> divideChecked(const double dividend, const double divisor) {
    if(divisor == 0.0) {
        return Result<double>::failure({.code = ErrorCode::InvalidArgument,
                                        .message = "divisor must not be zero",
                                        .path = "divisor",
                                        .details = std::nullopt});
    }
    return Result<double>::success(dividend / divisor);
}

Result<Value>
invoke(const Runtime& runtime, const std::string_view id_text, const Arguments& arguments) {
    const auto id = ActionId::parse(id_text);
    if(!id) {
        return Result<Value>::failure(id.error());
    }
    return runtime.invoke(id.value(), arguments, demoContext());
}

bool registerMathModule(Runtime& runtime) {
    ModuleBuilder math{
        ModuleDescriptor{.namespace_name = "math", .metadata = {{"title", "Mathematics"}}}};
    const auto prepared =
        math.add(
            "add", "Adds two integers",
            [](const int left, const int right) { return left + right; },
            param("left", "Left operand"), param("right", "Right operand")) &&
        math.add("divide", "Divides a dividend by a divisor", &divideChecked,
                 param("dividend", "Value to divide"), param("divisor", "Value that divides")) &&
        math.add(
            "scale", "Scales a value by an optional factor",
            [](const int value, const int factor) { return value * factor; },
            param("value", "Value to scale"), param("factor", "Scale factor", Value{2}));
    if(!prepared || !runtime.registerModule(std::move(math))) {
        std::puts("Failed to register the math module");
        return false;
    }
    return true;
}

bool registerAccumulatorModule(Runtime& runtime) {
    ModuleBuilder store{
        ModuleDescriptor{.namespace_name = "store", .metadata = {{"title", "In-memory store"}}}};
    const auto prepared =
        store.add("accumulate", "Adds a delta to an owned accumulator",
                  axiom::core::bindMember<&Accumulator::add>(std::make_shared<Accumulator>()),
                  param("delta", "Amount to add"));
    if(!prepared || !runtime.registerModule(std::move(store))) {
        std::puts("Failed to register the store module");
        return false;
    }
    return true;
}

bool showFrameworkIdentity() {
    const auto* name = axiom::core::frameworkName();
    std::printf("%s core walkthrough\n\n", name);
    if(!axiom::core::isFrameworkName(name)) {
        std::puts("frameworkName() and isFrameworkName() disagree");
        return false;
    }
    return true;
}

bool showDynamicValue() {
    std::puts("Value (JSON-shaped dynamic data):");
    const Value sample{Value::Object{
        {"enabled", Value{true}},
        {"items", Value{Value::Array{Value{1}, Value{2}}}},
        {"label", Value{"core"}},
    }};
    if(!sample.isObject()) {
        return false;
    }
    const auto& items = sample.asObject().at("items").asArray();
    std::printf("  fields=%zu items[1]=%lld label=%s\n\n", sample.asObject().size(),
                static_cast<long long>(items.at(1).asInteger()),
                sample.asObject().at("label").asString().c_str());
    return true;
}

void printModules(const Runtime& runtime) {
    std::puts("Modules:");
    for(const auto& module : runtime.discoverModules()) {
        std::printf("  %s", module.get().namespace_name.c_str());
        const auto title = module.get().metadata.find("title");
        if(title != module.get().metadata.end()) {
            std::printf(" (%s)", title->second.c_str());
        }
        std::puts("");
    }
}

void printAction(const ActionDescriptor& action) {
    std::printf("  ");
    printView(action.id.str());
    std::printf(" -> %s: %s\n", typeKindName(action.return_type.kind), action.description.c_str());
}

void printActions(const Runtime& runtime) {
    std::puts("Actions:");
    for(const auto& action : runtime.discoverActions()) {
        printAction(action.get());
    }
    std::puts("");
}

bool showDiscoveryLookup(const Runtime& runtime) {
    const auto module = runtime.findModule("math");
    const auto id = ActionId::parse("math.add");
    if(!module || !id) {
        std::puts("expected math.add to be discoverable");
        return false;
    }
    const auto action = runtime.findAction(id.value());
    if(!action) {
        std::puts("findAction(math.add) failed");
        return false;
    }
    std::printf("Lookup: module=%s action=", module.value().get().namespace_name.c_str());
    printView(action.value().get().id.action());
    std::puts("\n");
    return true;
}

bool showAddition(const Runtime& runtime) {
    std::puts("math.add(left=20, right=22)");
    const auto sum = invoke(runtime, "math.add", {{"left", Value{20}}, {"right", Value{22}}});
    if(!sum || !sum.value().isInteger()) {
        std::puts("  unexpected invocation failure");
        return false;
    }
    std::printf("  -> %lld\n", static_cast<long long>(sum.value().asInteger()));
    return true;
}

bool showOptionalParameter(const Runtime& runtime) {
    std::puts("math.scale(value=21)  // factor defaults to 2");
    const auto scaled = invoke(runtime, "math.scale", {{"value", Value{21}}});
    if(!scaled || !scaled.value().isInteger() || scaled.value().asInteger() != 42) {
        std::puts("  expected default factor 2");
        return false;
    }
    std::printf("  -> %lld\n", static_cast<long long>(scaled.value().asInteger()));
    return true;
}

bool showBoundMember(const Runtime& runtime) {
    std::puts("store.accumulate(delta=10) then store.accumulate(delta=5)");
    const auto first = invoke(runtime, "store.accumulate", {{"delta", Value{10}}});
    const auto second = invoke(runtime, "store.accumulate", {{"delta", Value{5}}});
    if(!first || !second || second.value().asInteger() != 15) {
        std::puts("  expected bindMember accumulator to retain state");
        return false;
    }
    std::printf("  -> %lld\n", static_cast<long long>(second.value().asInteger()));
    return true;
}

bool showBusinessError(const Runtime& runtime) {
    std::puts("math.divide(dividend=1.0, divisor=0.0)");
    const auto result =
        invoke(runtime, "math.divide", {{"dividend", Value{1.0}}, {"divisor", Value{0.0}}});
    if(result || result.error().code != ErrorCode::InvalidArgument) {
        std::puts("  expected an InvalidArgument business error");
        return false;
    }
    printError(result.error());
    return true;
}

bool showStructuralError(const Runtime& runtime) {
    std::puts("math.add(left=1, right=2, unexpected.key=3)");
    const auto result =
        invoke(runtime, "math.add",
               {{"left", Value{1}}, {"right", Value{2}}, {"unexpected.key", Value{3}}});
    if(result || result.error().code != ErrorCode::UnknownArgument) {
        std::puts("  expected an UnknownArgument structural error");
        return false;
    }
    printError(result.error());
    return true;
}

bool showUnknownAction(const Runtime& runtime) {
    std::puts("math.missing()");
    const auto result = invoke(runtime, "math.missing", {});
    if(result || result.error().code != ErrorCode::NotFound) {
        std::puts("  expected a NotFound error");
        return false;
    }
    printError(result.error());
    return true;
}

bool showInvalidActionId() {
    std::puts("ActionId::parse(\"Not.Canonical\")");
    const auto id = ActionId::parse("Not.Canonical");
    if(id || id.error().code != ErrorCode::InvalidArgument) {
        std::puts("  expected ActionId parsing to reject non-canonical text");
        return false;
    }
    printError(id.error());
    return true;
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

bool runInvocations(const Runtime& runtime) {
    return showAddition(runtime) && showOptionalParameter(runtime) && showBoundMember(runtime) &&
           showBusinessError(runtime) && showStructuralError(runtime) &&
           showUnknownAction(runtime) && showInvalidActionId();
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

int runDemo() {
    if(!showFrameworkIdentity() || !showDynamicValue()) {
        return EXIT_FAILURE;
    }

    DemoLogging logging;
    installSinks(logging);
    const auto app = logging.service.logger("demo", {{"session", Value{"walkthrough"}}});
    AXIOM_LOG_INFO(app, "starting core walkthrough");

    Runtime runtime{logging.service.logger("runtime")};
    if(!registerMathModule(runtime) || !registerAccumulatorModule(runtime)) {
        return EXIT_FAILURE;
    }
    printModules(runtime);
    printActions(runtime);

    const auto scenarios_ok = showDiscoveryLookup(runtime) && runInvocations(runtime) &&
                              showApplicationLogging(app) &&
                              summarizeLogs(*logging.collector, *logging.callback_count);
    logging.service.flush();
    if(!scenarios_ok) {
        return EXIT_FAILURE;
    }
    std::puts("\nDemo finished successfully");
    return EXIT_SUCCESS;
}

} // namespace

int main() {
    try {
        return runDemo();
    } catch(const std::exception& exception) {
        std::printf("Demo failed: %s\n", exception.what());
        return EXIT_FAILURE;
    } catch(...) {
        std::puts("Demo failed with an unexpected exception");
        return EXIT_FAILURE;
    }
}
