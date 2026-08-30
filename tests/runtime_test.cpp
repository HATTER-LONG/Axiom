#include <axiom/action/action_id.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/action/module.hpp>
#include <axiom/action/module_builder.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/logging/log_level.hpp>
#include <axiom/logging/log_record.hpp>
#include <axiom/logging/log_sink.hpp>
#include <axiom/logging/logging_service.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using axiom::ActionId;
using axiom::Arguments;
using axiom::ErrorCode;
using axiom::InvocationContext;
using axiom::ModuleBuilder;
using axiom::param;
using axiom::Result;
using axiom::Runtime;
using axiom::Value;
using axiom::logging::ILogSink;
using axiom::logging::LoggingService;
using axiom::logging::LogLevel;
using axiom::logging::LogRecord;

class RuntimeRecordingSink final : public ILogSink {
public:
    void consume(const LogRecord& record) override { records.push_back(record); }

    std::vector<LogRecord> records;
};

class RuntimeThrowingSink final : public ILogSink {
public:
    void consume(const LogRecord& record) override {
        static_cast<void>(record);
        throw std::runtime_error{"sink failure"};
    }
};

int add(const int left, const int right) { return left + right; }

Result<int> checked(const int value) {
    if(value < 0) {
        return Result<int>::failure({.code = ErrorCode::InvalidArgument,
                                     .message = "value must not be negative",
                                     .path = "value",
                                     .details = std::nullopt});
    }
    return Result<int>::success(value);
}

Result<void> checkedVoid(const bool allowed) {
    if(!allowed) {
        return Result<void>::failure({.code = ErrorCode::InvalidArgument,
                                      .message = "permission denied",
                                      .path = "allowed",
                                      .details = std::nullopt});
    }
    return Result<void>::success();
}

void throwStandard() { throw std::runtime_error{"unexpected"}; }
void throwUnknown() { throw 7; }
void plainVoid() {}
using RvalueOnly = int (*)(std::string&&);

template <typename T> [[nodiscard]] T transferOwnership(T& source) { return std::move(source); }
double sumMap(const std::map<std::string, double>& values) {
    double total = 0.0;
    for(const auto& [name, value] : values) {
        static_cast<void>(name);
        total += value;
    }
    return total;
}

struct OverloadedCallable {
    int operator()(const int value) const { return value; }
    int operator()(const std::string& value) const { return static_cast<int>(value.size()); }
};

struct NonCopyableCallable {
    NonCopyableCallable() = default;
    NonCopyableCallable(const NonCopyableCallable&) = delete;
    NonCopyableCallable& operator=(const NonCopyableCallable&) = delete;
    NonCopyableCallable(NonCopyableCallable&&) = default;
    NonCopyableCallable& operator=(NonCopyableCallable&&) = default;

    int operator()(const int value) const { return value; }
};

class Multiplier {
public:
    explicit Multiplier(const int factor) : factor_(factor) {}
    [[nodiscard]] int multiply(const int value) const { return value * factor_; }

private:
    int factor_;
};

template <typename Callable, typename... Documentation>
concept PubliclyAddable =
    requires(ModuleBuilder& builder, Callable&& callable, Documentation&&... documentation) {
        builder.add("probe", "Compile-time registration probe", std::forward<Callable>(callable),
                    std::forward<Documentation>(documentation)...);
    };

using ParameterDoc = axiom::ParameterDocumentation;
using CopyableLambda = decltype([](const int value) { return value; });

struct NonDefaultConstructibleCompare {
    explicit NonDefaultConstructibleCompare(const int tag) : tag(tag) {}
    [[nodiscard]] bool operator()(const std::string& left, const std::string& right) const {
        return left < right;
    }

    int tag;
};

struct NonDefaultConstructibleHash {
    explicit NonDefaultConstructibleHash(const int tag) : tag(tag) {}
    [[nodiscard]] std::size_t operator()(const std::string& value) const {
        return std::hash<std::string>{}(value);
    }

    int tag;
};

template <typename T> struct NonDefaultConstructibleAllocator {
    // NOLINTNEXTLINE(readability-identifier-naming): allocator_traits mandates value_type.
    using value_type = T;

    explicit NonDefaultConstructibleAllocator(const int tag) : tag(tag) {}
    template <typename U>
    explicit NonDefaultConstructibleAllocator(const NonDefaultConstructibleAllocator<U>& other)
        : tag(other.tag) {}

    [[nodiscard]] static T* allocate(const std::size_t count) {
        return std::allocator<T>{}.allocate(count);
    }
    static void deallocate(T* const pointer, const std::size_t count) {
        std::allocator<T>{}.deallocate(pointer, count);
    }
    template <typename U>
    [[nodiscard]] bool operator==(const NonDefaultConstructibleAllocator<U>& other) const noexcept {
        static_cast<void>(other);
        return true;
    }

    int tag;
};

using PolicyMap = std::map<std::string, int, NonDefaultConstructibleCompare>;
using PolicyHashMap = std::unordered_map<std::string, int, NonDefaultConstructibleHash>;
using PolicyVector = std::vector<int, NonDefaultConstructibleAllocator<int>>;

using PolicyEntriesCallable = int (*)(const PolicyMap&);
using PolicyHashMapCallable = int (*)(const PolicyHashMap&);
using PolicyVectorCallable = int (*)(const PolicyVector&);

static_assert(PubliclyAddable<decltype(add)&, ParameterDoc, ParameterDoc>);
static_assert(PubliclyAddable<decltype(&add), ParameterDoc, ParameterDoc>);
static_assert(PubliclyAddable<CopyableLambda, ParameterDoc>);
static_assert(!PubliclyAddable<RvalueOnly, ParameterDoc>);
static_assert(!PubliclyAddable<decltype([](const auto value) { return value; }), ParameterDoc>);
static_assert(!PubliclyAddable<OverloadedCallable, ParameterDoc>);
static_assert(!PubliclyAddable<NonCopyableCallable, ParameterDoc>);
static_assert(!PubliclyAddable<decltype(&Multiplier::multiply), ParameterDoc>);
static_assert(!PubliclyAddable<std::function<int(int)>, ParameterDoc>);
static_assert(!PubliclyAddable<PolicyEntriesCallable, ParameterDoc>);
static_assert(!PubliclyAddable<PolicyHashMapCallable, ParameterDoc>);
static_assert(!PubliclyAddable<PolicyVectorCallable, ParameterDoc>);

ActionId id(const std::string_view text) {
    const auto parsed = ActionId::parse(text);
    EXPECT_TRUE(parsed);
    return parsed.value();
}

ModuleBuilder mathBuilder() {
    return ModuleBuilder{
        axiom::ModuleDescriptor{.namespace_name = "math", .metadata = {{"title", "Math"}}}};
}

void addArithmeticActions(ModuleBuilder& math) {
    EXPECT_TRUE(math.add("add", "Adds two integers", add, param("left", "Left operand"),
                         param("right", "Right operand")));
    EXPECT_TRUE(math.add("checked", "Returns a business error", &checked,
                         param("value", "Candidate value")));
}

void addResultAndVoidActions(ModuleBuilder& math) {
    EXPECT_TRUE(math.add("checked_void", "Returns a valueless result", &checkedVoid,
                         param("allowed", "Whether the call is allowed")));
    EXPECT_TRUE(math.add("plain_void", "Returns no value", &plainVoid));
}

void addExceptionActions(ModuleBuilder& math) {
    EXPECT_TRUE(math.add("standard", "Throws a standard exception", &throwStandard));
    EXPECT_TRUE(math.add("unknown", "Throws an unknown exception", &throwUnknown));
}

void addMemberAction(ModuleBuilder& math) {
    EXPECT_TRUE(math.add("multiply", "Uses an explicitly owned object",
                         axiom::bindMember<&Multiplier::multiply>(std::make_shared<Multiplier>(3)),
                         param("value", "Operand")));
}

void configureRuntime(Runtime& runtime) {
    auto math = mathBuilder();
    addArithmeticActions(math);
    addResultAndVoidActions(math);
    addExceptionActions(math);
    addMemberAction(math);
    EXPECT_TRUE(runtime.registerModule(std::move(math)));
}

void expectInvalidActionName(const Result<void>& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

void expectInvalidParameter(const Result<void>& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::InvalidDescriptor);
}

void expectAddedAction(const Result<void>& result) { ASSERT_TRUE(result); }

void expectDuplicateAction(const Result<void>& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::AlreadyExists);
}

void expectRejectedDefault(const Result<void>& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::InvalidDescriptor);
}

void expectEmptyBuilderFailure(const Result<void>& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

Result<void> addLateAction(ModuleBuilder& builder) {
    return builder.add("late", "Rejected after ownership transfer", [] { return 1; });
}

const LogRecord& recordAt(const RuntimeRecordingSink& sink, const std::size_t index) {
    EXPECT_LT(index, sink.records.size());
    return sink.records.at(index);
}

} // namespace

TEST(Runtime, InvokesTypedFunctionsAndDiscoversStableDescriptors) {
    Runtime runtime;
    configureRuntime(runtime);

    const auto result =
        runtime.invoke(id("math.add"), Arguments{{"left", Value{2}}, {"right", Value{3}}}, {});
    const auto member = runtime.invoke(id("math.multiply"), Arguments{{"value", Value{4}}}, {});
    const auto modules = runtime.discoverModules();
    const auto actions = runtime.discoverActions();
    const auto found_module = runtime.findModule("math");
    const auto missing_module = runtime.findModule("missing");

    ASSERT_TRUE(result);
    ASSERT_TRUE(member);
    EXPECT_EQ(result.value().asInteger(), 5);
    EXPECT_EQ(member.value().asInteger(), 12);
    ASSERT_EQ(modules.size(), 1U);
    EXPECT_EQ(modules.front().get().namespace_name, "math");
    ASSERT_TRUE(found_module);
    EXPECT_EQ(found_module.value().get().namespace_name, "math");
    ASSERT_FALSE(missing_module);
    EXPECT_EQ(missing_module.error().code, ErrorCode::NotFound);
    ASSERT_EQ(actions.size(), 7U);
    EXPECT_EQ(actions.front().get().id.str(), "math.add");
    const auto descriptor = runtime.findAction(id("math.add"));
    ASSERT_TRUE(descriptor);
    EXPECT_EQ(descriptor.value().get().parameters[0].type.kind,
              axiom::TypeDescriptor::Kind::Integer);
}

TEST(Runtime, PreservesBusinessErrorsAndSupportsVoidResults) {
    Runtime runtime;
    configureRuntime(runtime);

    const auto business = runtime.invoke(id("math.checked"), Arguments{{"value", Value{-1}}}, {});
    const auto result_void =
        runtime.invoke(id("math.checked_void"), Arguments{{"allowed", Value{true}}}, {});
    const auto plain_void = runtime.invoke(id("math.plain_void"), {}, {});
    const auto error_void =
        runtime.invoke(id("math.checked_void"), Arguments{{"allowed", Value{false}}}, {});

    ASSERT_FALSE(business);
    EXPECT_EQ(business.error().code, ErrorCode::InvalidArgument);
    const auto& business_path = business.error().path;
    ASSERT_TRUE(business_path.has_value());
    if(!business_path.has_value()) {
        return;
    }
    EXPECT_EQ(business_path.value(), "value");
    ASSERT_TRUE(result_void);
    EXPECT_TRUE(result_void.value().isNull());
    ASSERT_TRUE(plain_void);
    EXPECT_TRUE(plain_void.value().isNull());
    ASSERT_FALSE(error_void);
    EXPECT_EQ(error_void.error().code, ErrorCode::InvalidArgument);
}

TEST(Runtime, ReportsStructuralAndConversionErrorsAtPublicPaths) {
    Runtime runtime;
    configureRuntime(runtime);

    const auto unknown = runtime.invoke(id("math.missing"), {}, {});
    const auto missing = runtime.invoke(id("math.add"), Arguments{{"left", Value{1}}}, {});
    const auto extra =
        runtime.invoke(id("math.add"),
                       Arguments{{"left", Value{1}}, {"right", Value{2}}, {"extra", Value{3}}}, {});
    const auto mismatch =
        runtime.invoke(id("math.add"), Arguments{{"left", Value{"one"}}, {"right", Value{2}}}, {});

    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code, ErrorCode::NotFound);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, ErrorCode::MissingArgument);
    const auto& missing_path = missing.error().path;
    ASSERT_TRUE(missing_path.has_value());
    if(!missing_path.has_value()) {
        return;
    }
    EXPECT_EQ(missing_path.value(), "right");
    ASSERT_FALSE(extra);
    EXPECT_EQ(extra.error().code, ErrorCode::UnknownArgument);
    const auto& extra_path = extra.error().path;
    ASSERT_TRUE(extra_path.has_value());
    if(!extra_path.has_value()) {
        return;
    }
    EXPECT_EQ(extra_path.value(), "extra");
    ASSERT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error().code, ErrorCode::TypeMismatch);
    const auto& mismatch_path = mismatch.error().path;
    ASSERT_TRUE(mismatch_path.has_value());
    if(!mismatch_path.has_value()) {
        return;
    }
    EXPECT_EQ(mismatch_path.value(), "left");
}

TEST(Runtime, NormalizesStandardAndUnknownCallableExceptions) {
    Runtime runtime;
    configureRuntime(runtime);

    const auto standard = runtime.invoke(id("math.standard"), {}, {});
    const auto unknown = runtime.invoke(id("math.unknown"), {}, {});

    ASSERT_FALSE(standard);
    ASSERT_FALSE(unknown);
    EXPECT_EQ(standard.error().code, ErrorCode::InvocationFailed);
    EXPECT_EQ(unknown.error().code, ErrorCode::InternalError);
}

TEST(Runtime, RejectsDuplicateAndInvalidModuleRegistrationsWithoutChangingRuntime) {
    Runtime runtime;
    configureRuntime(runtime);
    ModuleBuilder duplicate{axiom::ModuleDescriptor{.namespace_name = "math", .metadata = {}}};
    EXPECT_TRUE(duplicate.add("second", "Second action", [] { return 2; }));
    ModuleBuilder invalid{
        axiom::ModuleDescriptor{.namespace_name = "invalid-module", .metadata = {}}};

    const auto duplicate_result = runtime.registerModule(std::move(duplicate));
    const auto invalid_result = runtime.registerModule(std::move(invalid));

    ASSERT_FALSE(duplicate_result);
    ASSERT_FALSE(invalid_result);
    EXPECT_EQ(duplicate_result.error().code, ErrorCode::AlreadyExists);
    EXPECT_EQ(invalid_result.error().code, ErrorCode::InvalidDescriptor);
    EXPECT_EQ(runtime.discoverModules().size(), 1U);
    EXPECT_EQ(runtime.discoverActions().size(), 7U);
}

TEST(Runtime, EmitsStructuredRegistrationAndInvocationOutcomes) {
    LoggingService logging;
    const auto sink = std::make_shared<RuntimeRecordingSink>();
    auto subscription = logging.addSink(sink);
    Runtime runtime{logging.logger("runtime")};
    configureRuntime(runtime);

    const auto success =
        runtime.invoke(id("math.add"), Arguments{{"left", Value{2}}, {"right", Value{3}}}, {});
    const auto expected_failure = runtime.invoke(id("math.checked"), {{"value", Value{-1}}}, {});
    const auto exception_failure = runtime.invoke(id("math.standard"), {}, {});
    const auto unknown_exception_failure = runtime.invoke(id("math.unknown"), {}, {});

    ASSERT_TRUE(success);
    EXPECT_EQ(success.value().asInteger(), 5);
    ASSERT_FALSE(expected_failure);
    EXPECT_EQ(expected_failure.error().code, ErrorCode::InvalidArgument);
    ASSERT_FALSE(exception_failure);
    EXPECT_EQ(exception_failure.error().code, ErrorCode::InvocationFailed);
    ASSERT_FALSE(unknown_exception_failure);
    EXPECT_EQ(unknown_exception_failure.error().code, ErrorCode::InternalError);

    ASSERT_EQ(sink->records.size(), 9U);
    const auto& registration = recordAt(*sink, 0U);
    EXPECT_EQ(registration.category, "runtime.module");
    EXPECT_EQ(registration.level, LogLevel::Info);
    EXPECT_EQ(registration.fields.at("module").asString(), "math");

    const auto& start = recordAt(*sink, 1U);
    EXPECT_EQ(start.category, "runtime.action");
    EXPECT_EQ(start.level, LogLevel::Debug);
    EXPECT_EQ(start.fields.at("module").asString(), "math");
    EXPECT_EQ(start.fields.at("action").asString(), "math.add");

    const auto& successful_end = recordAt(*sink, 2U);
    EXPECT_EQ(successful_end.level, LogLevel::Info);
    EXPECT_EQ(successful_end.fields.at("status").asString(), "success");
    EXPECT_EQ(successful_end.fields.at("module").asString(), "math");
    EXPECT_EQ(successful_end.fields.at("action").asString(), "math.add");
    EXPECT_GE(successful_end.fields.at("duration_ms").asInteger(), 0);

    const auto& expected_end = recordAt(*sink, 4U);
    EXPECT_EQ(expected_end.level, LogLevel::Warning);
    EXPECT_EQ(expected_end.fields.at("status").asString(), "invalid_argument");

    const auto& exception_end = recordAt(*sink, 6U);
    EXPECT_EQ(exception_end.level, LogLevel::Error);
    EXPECT_EQ(exception_end.fields.at("status").asString(), "invocation_failed");

    const auto& unknown_exception_end = recordAt(*sink, 8U);
    EXPECT_EQ(unknown_exception_end.level, LogLevel::Error);
    EXPECT_EQ(unknown_exception_end.fields.at("status").asString(), "internal_error");
}

TEST(Runtime, RecordsExpectedRegistrationFailuresAsWarnings) {
    LoggingService logging;
    const auto sink = std::make_shared<RuntimeRecordingSink>();
    auto subscription = logging.addSink(sink);
    Runtime runtime{logging.logger("runtime")};
    ModuleBuilder invalid{
        axiom::ModuleDescriptor{.namespace_name = "invalid-module", .metadata = {}}};
    ModuleBuilder first{axiom::ModuleDescriptor{.namespace_name = "duplicate", .metadata = {}}};
    ModuleBuilder duplicate{axiom::ModuleDescriptor{.namespace_name = "duplicate", .metadata = {}}};
    ASSERT_TRUE(first.add("first", "First action", [] { return 1; }));
    ASSERT_TRUE(duplicate.add("second", "Second action", [] { return 2; }));

    const auto invalid_result = runtime.registerModule(std::move(invalid));
    ASSERT_TRUE(runtime.registerModule(std::move(first)));
    const auto duplicate_result = runtime.registerModule(std::move(duplicate));

    ASSERT_FALSE(invalid_result);
    EXPECT_EQ(invalid_result.error().code, ErrorCode::InvalidDescriptor);
    ASSERT_FALSE(duplicate_result);
    EXPECT_EQ(duplicate_result.error().code, ErrorCode::AlreadyExists);
    ASSERT_EQ(sink->records.size(), 3U);
    EXPECT_EQ(recordAt(*sink, 0U).category, "runtime.module");
    EXPECT_EQ(recordAt(*sink, 0U).level, LogLevel::Warning);
    EXPECT_EQ(recordAt(*sink, 1U).level, LogLevel::Info);
    EXPECT_EQ(recordAt(*sink, 2U).level, LogLevel::Warning);
}

TEST(Runtime, LogsAnEmptyBuilderRegistrationFailureWithoutAModuleField) {
    LoggingService logging;
    const auto sink = std::make_shared<RuntimeRecordingSink>();
    auto subscription = logging.addSink(sink);
    Runtime runtime{logging.logger("runtime")};
    ModuleBuilder source{axiom::ModuleDescriptor{.namespace_name = "temporary", .metadata = {}}};
    ModuleBuilder empty{transferOwnership(source)};

    const auto result = runtime.registerModule(std::move(source));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    ASSERT_EQ(sink->records.size(), 1U);
    const auto& logged = recordAt(*sink, 0U);
    EXPECT_EQ(logged.category, "runtime.module");
    EXPECT_EQ(logged.level, LogLevel::Warning);
    EXPECT_EQ(logged.message, "module registration failed: invalid_argument");
    EXPECT_FALSE(logged.fields.contains("module"));
    static_cast<void>(empty);
}

TEST(Runtime, PropagatesInvocationContextAndOverridesRuntimeFields) {
    LoggingService logging;
    const auto sink = std::make_shared<RuntimeRecordingSink>();
    auto subscription = logging.addSink(sink);
    const auto business_logger = logging.logger("business");
    ModuleBuilder builder{axiom::ModuleDescriptor{.namespace_name = "context", .metadata = {}}};
    ASSERT_TRUE(builder.add("emit", "Emits a business record", [business_logger] {
        business_logger.write(LogLevel::Info, "business event");
        return 42;
    }));
    Runtime runtime{logging.logger("runtime")};
    ASSERT_TRUE(runtime.registerModule(std::move(builder)));

    const InvocationContext context{.request_id = "request-7",
                                    .trace_id = "trace-9",
                                    .caller = "client",
                                    .metadata = {{"tenant", "north"},
                                                 {"module", "metadata-module"},
                                                 {"action", "metadata-action"},
                                                 {"status", "metadata-status"},
                                                 {"duration_ms", "metadata-duration"}}};
    const auto result = runtime.invoke(id("context.emit"), {}, context);

    ASSERT_TRUE(result);
    ASSERT_EQ(sink->records.size(), 4U);
    const auto& business_record = recordAt(*sink, 2U);
    EXPECT_EQ(business_record.category, "business");
    EXPECT_EQ(business_record.fields.at("request_id").asString(), "request-7");
    EXPECT_EQ(business_record.fields.at("trace_id").asString(), "trace-9");
    EXPECT_EQ(business_record.fields.at("caller").asString(), "client");
    EXPECT_EQ(business_record.fields.at("tenant").asString(), "north");
    EXPECT_EQ(business_record.fields.at("module").asString(), "context");
    EXPECT_EQ(business_record.fields.at("action").asString(), "context.emit");
    EXPECT_FALSE(business_record.fields.contains("status"));
    EXPECT_FALSE(business_record.fields.contains("duration_ms"));

    const auto& finish = recordAt(*sink, 3U);
    EXPECT_EQ(finish.fields.at("module").asString(), "context");
    EXPECT_EQ(finish.fields.at("action").asString(), "context.emit");
    EXPECT_EQ(finish.fields.at("status").asString(), "success");
    EXPECT_GE(finish.fields.at("duration_ms").asInteger(), 0);
}

TEST(Runtime, IncludesOnlyProvidedInvocationContextFieldsInLogs) {
    LoggingService logging;
    const auto sink = std::make_shared<RuntimeRecordingSink>();
    auto subscription = logging.addSink(sink);
    Runtime runtime{logging.logger("runtime")};
    ModuleBuilder builder{
        axiom::ModuleDescriptor{.namespace_name = "context_fields", .metadata = {}}};
    ASSERT_TRUE(builder.add("run", "Returns a fixed value", [] { return 1; }));
    ASSERT_TRUE(runtime.registerModule(std::move(builder)));

    const InvocationContext populated{.request_id = "request-1",
                                      .trace_id = "trace-2",
                                      .caller = "caller-3",
                                      .metadata = {{"tenant", "north"}}};
    ASSERT_TRUE(runtime.invoke(id("context_fields.run"), {}, populated));
    ASSERT_TRUE(runtime.invoke(id("context_fields.run"), {}, {}));

    ASSERT_EQ(sink->records.size(), 5U);
    const auto& populated_start = recordAt(*sink, 1U);
    EXPECT_EQ(populated_start.fields.at("request_id").asString(), "request-1");
    EXPECT_EQ(populated_start.fields.at("trace_id").asString(), "trace-2");
    EXPECT_EQ(populated_start.fields.at("caller").asString(), "caller-3");
    EXPECT_EQ(populated_start.fields.at("tenant").asString(), "north");

    const auto& empty_start = recordAt(*sink, 3U);
    EXPECT_FALSE(empty_start.fields.contains("request_id"));
    EXPECT_FALSE(empty_start.fields.contains("trace_id"));
    EXPECT_FALSE(empty_start.fields.contains("caller"));
    EXPECT_FALSE(empty_start.fields.contains("tenant"));
    EXPECT_EQ(empty_start.fields.at("module").asString(), "context_fields");
    EXPECT_EQ(empty_start.fields.at("action").asString(), "context_fields.run");
}

TEST(Runtime, KeepsExecutionAndResultsWhenASinkThrows) {
    LoggingService logging;
    const auto good_sink = std::make_shared<RuntimeRecordingSink>();
    auto bad_subscription = logging.addSink(std::make_shared<RuntimeThrowingSink>());
    auto good_subscription = logging.addSink(good_sink);
    int invocations = 0;
    ModuleBuilder builder{axiom::ModuleDescriptor{.namespace_name = "resilient", .metadata = {}}};
    ASSERT_TRUE(builder.add("run", "Counts executions", [&invocations] {
        ++invocations;
        return 7;
    }));
    Runtime runtime{logging.logger("runtime")};
    ASSERT_TRUE(runtime.registerModule(std::move(builder)));

    const auto result = runtime.invoke(id("resilient.run"), {}, {});

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().asInteger(), 7);
    EXPECT_EQ(invocations, 1);
    EXPECT_EQ(good_sink->records.size(), 3U);
}

TEST(Runtime, PreservesDefaultNoLoggerInvocationBehavior) {
    int invocations = 0;
    ModuleBuilder builder{axiom::ModuleDescriptor{.namespace_name = "silent", .metadata = {}}};
    ASSERT_TRUE(builder.add("run", "Counts executions", [&invocations] {
        ++invocations;
        return 11;
    }));
    Runtime runtime;
    ASSERT_TRUE(runtime.registerModule(std::move(builder)));

    const auto result = runtime.invoke(id("silent.run"), {}, {});

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().asInteger(), 11);
    EXPECT_EQ(invocations, 1);
}

TEST(Runtime, InvokesCopyableLambdaRegisteredFromItsInferredSignature) {
    ModuleBuilder builder{axiom::ModuleDescriptor{.namespace_name = "lambda", .metadata = {}}};
    int offset = 5;

    EXPECT_TRUE(builder.add(
        "offset", "Adds a captured offset", [offset](const int value) { return value + offset; },
        param("value", "Input value")));
    offset = 100;

    Runtime runtime;
    ASSERT_TRUE(runtime.registerModule(std::move(builder)));
    const auto result = runtime.invoke(id("lambda.offset"), Arguments{{"value", Value{7}}}, {});

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().asInteger(), 12);
}

TEST(Runtime, DescribesAndConvertsHomogeneousStringKeyMaps) {
    ModuleBuilder builder{axiom::ModuleDescriptor{.namespace_name = "maps", .metadata = {}}};
    ASSERT_TRUE(builder.add("sum", "Sums values by name", &sumMap, param("values", "Values")));
    Runtime runtime;
    ASSERT_TRUE(runtime.registerModule(std::move(builder)));

    const auto descriptor = runtime.findAction(id("maps.sum"));
    const auto invoked = runtime.invoke(
        id("maps.sum"),
        Arguments{{"values", Value{Value::Object{{"left", Value{1}}, {"right", Value{2.5}}}}}}, {});

    ASSERT_TRUE(descriptor);
    const auto& type = descriptor.value().get().parameters.front().type;
    ASSERT_NE(type.value_type, nullptr);
    EXPECT_TRUE(type.fields.empty());
    EXPECT_EQ(type.kind, axiom::TypeDescriptor::Kind::Object);
    EXPECT_EQ(type.value_type->kind, axiom::TypeDescriptor::Kind::Number);
    ASSERT_TRUE(invoked);
    EXPECT_DOUBLE_EQ(invoked.value().asNumber(), 3.5);
}

TEST(Runtime, ConvertsCompatibleIntegerDefaultsForInferredNumberParameters) {
    ModuleBuilder builder{axiom::ModuleDescriptor{.namespace_name = "defaults", .metadata = {}}};
    ASSERT_TRUE(builder.add(
        "scale", "Scales an inferred number", [](const double factor) { return factor * 1.5; },
        param("factor", "Optional factor", Value{std::int64_t{2}})));
    Runtime runtime;
    ASSERT_TRUE(runtime.registerModule(std::move(builder)));

    const auto descriptor = runtime.findAction(id("defaults.scale"));
    const auto invoked = runtime.invoke(id("defaults.scale"), {}, {});

    ASSERT_TRUE(descriptor);
    EXPECT_FALSE(descriptor.value().get().parameters.front().required);
    ASSERT_TRUE(descriptor.value().get().parameters.front().default_value.has_value());
    ASSERT_TRUE(invoked);
    EXPECT_DOUBLE_EQ(invoked.value().asNumber(), 3.0);
}

TEST(ModuleBuilder, RejectsDefaultsThatCannotConvertToInferredCppTypes) {
    ModuleBuilder builder{axiom::ModuleDescriptor{.namespace_name = "defaults", .metadata = {}}};
    const Value narrow_default{std::int64_t{128}};
    const Value nested_default{Value::Array{Value{Value::Array{Value{std::int64_t{128}}}}}};

    const auto narrow = builder.add(
        "narrow", "Rejects an overflowing default", [](const std::int8_t value) { return value; },
        param("value", "Narrow value", narrow_default));
    const auto nested = builder.add(
        "nested", "Rejects an overflowing nested default",
        [](const std::vector<std::vector<std::int8_t>>& values) {
            return static_cast<int>(values.size());
        },
        param("values", "Nested narrow values", nested_default));
    const auto accepted = builder.add(
        "number", "Accepts integer to number conversion", [](const double value) { return value; },
        param("value", "Number", Value{std::int64_t{2}}));

    expectRejectedDefault(narrow);
    expectRejectedDefault(nested);
    expectAddedAction(accepted);

    Runtime runtime;
    ASSERT_TRUE(runtime.registerModule(std::move(builder)));
    EXPECT_EQ(runtime.discoverActions().size(), 1U);
}

TEST(ModuleBuilder, RejectsRegistrationAfterRuntimeConsumedTheBuilder) {
    ModuleBuilder builder{axiom::ModuleDescriptor{.namespace_name = "consumed", .metadata = {}}};
    ASSERT_TRUE(builder.add("first", "Valid action", [] { return 1; }));
    Runtime runtime;
    ASSERT_TRUE(runtime.registerModule(std::move(builder)));

    const auto consumed = addLateAction(builder);

    expectEmptyBuilderFailure(consumed);
    EXPECT_EQ(runtime.discoverActions().size(), 1U);
}

TEST(ModuleBuilder, RejectsUseAfterMoveAndRegistrationOfEmptyBuilders) {
    ModuleBuilder source{axiom::ModuleDescriptor{.namespace_name = "moved", .metadata = {}}};
    ModuleBuilder destination{std::move(source)};

    const auto moved_from = addLateAction(source);
    Runtime runtime;
    const auto empty_registration = runtime.registerModule(std::move(source));

    expectEmptyBuilderFailure(moved_from);
    expectEmptyBuilderFailure(empty_registration);
    ASSERT_TRUE(runtime.registerModule(std::move(destination)));
    EXPECT_EQ(runtime.discoverModules().size(), 1U);
}

TEST(ModuleBuilder, RejectsInvalidAndDuplicateActionDefinitionsWithoutStateMutation) {
    ModuleBuilder builder{axiom::ModuleDescriptor{.namespace_name = "builder", .metadata = {}}};

    const auto invalid_name =
        builder.add("not-valid", "Invalid local identifier", [] { return 1; });
    const auto invalid_parameter = builder.add(
        "value", "Invalid parameter identifier", [](const int value) { return value; },
        param("not-valid", "Invalid"));
    const auto added = builder.add(
        "value", "Valid action", [](const int value) { return value; }, param("value", "Valid"));
    const auto duplicate = builder.add(
        "value", "Duplicate action", [](const int value) { return value; },
        param("value", "Valid"));

    expectInvalidActionName(invalid_name);
    expectInvalidParameter(invalid_parameter);
    expectAddedAction(added);
    expectDuplicateAction(duplicate);

    Runtime runtime;
    ASSERT_TRUE(runtime.registerModule(std::move(builder)));
    EXPECT_EQ(runtime.discoverActions().size(), 1U);
}
