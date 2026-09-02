#include "command/command_validation.hpp"
#include "command/descriptor_conversion.hpp"

#include <axiom/action/action_id.hpp>
#include <axiom/action/descriptor.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/action/module_builder.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/async/executor.hpp>
#include <axiom/command/command_dispatcher.hpp>
#include <axiom/command/command_methods.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/introspection/introspection_query.hpp>
#include <axiom/introspection/introspection_service.hpp>
#include <axiom/resource/resource_id.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/resource/resource_traits.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_registry.hpp>
#include <axiom/task/task_types.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <initializer_list>
#include <iterator>
#include <latch>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct Widget final {};

} // namespace

template <> struct axiom::resource::ResourceTraits<::Widget> {
    static constexpr std::string_view type_name = "widget";
};

namespace {

using axiom::ActionId;
using axiom::ActionInvocation;
using axiom::Error;
using axiom::ErrorCode;
using axiom::InvocationContext;
using axiom::ModuleBuilder;
using axiom::ParameterDescriptor;
using axiom::Result;
using axiom::Runtime;
using axiom::TypeDescriptor;
using axiom::Value;
using axiom::command::action_describe;
using axiom::command::action_invoke;
using axiom::command::action_list;
using axiom::command::CommandDispatcher;
using axiom::command::resource_describe;
using axiom::command::resource_list;
using axiom::command::system_snapshot;
using axiom::command::task_cancel;
using axiom::command::task_describe;
using axiom::command::task_list;

struct ProbeState {
    std::atomic<int> invokes{0};
    InvocationContext last_context;
};

[[nodiscard]] Value::Object object(std::initializer_list<Value::Object::value_type> fields) {
    return Value::Object{fields};
}

[[nodiscard]] std::vector<Value> nonMatchingTypes(const Value::Type allowed) {
    std::vector<Value> values{
        Value{nullptr}, Value{true},           Value{std::int64_t{1}}, Value{1.5},
        Value{"text"},  Value{Value::Array{}}, Value{object({})},
    };
    std::erase_if(values, [allowed](const Value& value) { return value.type() == allowed; });
    return values;
}

void expectError(const Result<Value>& result,
                 const ErrorCode code,
                 const std::optional<std::string>& path) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, code);
    EXPECT_EQ(result.error().path, path);
}

void expectTypeMismatch(const CommandDispatcher& dispatcher,
                        const std::string_view method,
                        const Value::Object& params,
                        const std::string& field) {
    for(const auto& wrong : nonMatchingTypes(Value::Type::String)) {
        auto candidate = params;
        candidate.insert_or_assign(field, wrong);
        expectError(dispatcher.dispatch(method, candidate, {}), ErrorCode::TypeMismatch, field);
    }
}

void registerCatalog(Runtime& runtime, ProbeState& probe) {
    ModuleBuilder math{{.namespace_name = "math",
                        .description = "Math module",
                        .version = "1.0",
                        .tags = {"core"},
                        .metadata = {{"owner", "tests"}}}};
    EXPECT_TRUE(math.add(
        "add", "Add two integers", [](const int left, const int right) { return left + right; },
        axiom::param("left", "Left"), axiom::param("right", "Right")));
    EXPECT_TRUE(math.add("ping", "Ping", [] { return 1; }));
    EXPECT_TRUE(math.addContextual("probe", "Records diagnostic context",
                                   [&probe](const ActionInvocation& invocation) {
                                       ++probe.invokes;
                                       probe.last_context = invocation.context();
                                       return 7;
                                   }));
    EXPECT_TRUE(math.add("fail", "Business failure", []() -> Result<int> {
        return Result<int>::failure({.code = ErrorCode::InvalidArgument,
                                     .message = "shape is invalid",
                                     .path = "shape.size",
                                     .details = std::nullopt});
    }));
    EXPECT_TRUE(math.add("boom", "Throws std::exception",
                         []() -> int { throw std::runtime_error{"unexpected"}; }));
    EXPECT_TRUE(math.add("explode", "Throws an unknown type", []() -> int { throw 7; }));
    EXPECT_TRUE(runtime.registerModule(std::move(math)));
}

void registerNested(Runtime& runtime) {
    ModuleBuilder nested{{.namespace_name = "catalog",
                          .description = "Catalog",
                          .version = {},
                          .tags = {"search"},
                          .metadata = {}}};
    EXPECT_TRUE(nested.add(
        "inspect", "Inspect nested values",
        [](const std::vector<std::vector<int>>& values, const std::map<std::string, int>& fields) {
            return static_cast<int>(values.size() + fields.size());
        },
        axiom::param("values", "Nested arrays"), axiom::param("fields", "Map")));
    EXPECT_TRUE(nested.add(
        "label", "Optional label", [](const std::string& label) { return label; },
        axiom::param("label", "Label", Value{"none"})));
    EXPECT_TRUE(runtime.registerModule(std::move(nested)));
}

struct CommandFixture {
    ProbeState probe;
    Runtime runtime;
    axiom::resource::ResourceRegistry resources;
    axiom::task::TaskRegistry tasks;
    CommandDispatcher dispatcher{runtime, resources, tasks};

    CommandFixture() {
        registerCatalog(runtime, probe);
        registerNested(runtime);
    }
};

[[nodiscard]] axiom::task::TaskOrigin testOrigin(std::string request, std::string action) {
    return {.request_id = std::move(request),
            .trace_id = "trace",
            .caller = "suite",
            .action_id = std::move(action),
            .metadata = {}};
}

[[nodiscard]] std::vector<std::string> stringIds(const Value& list, const char* key) {
    std::vector<std::string> ids;
    const auto& items = list.asArray();
    ids.reserve(items.size());
    std::ranges::transform(items, std::back_inserter(ids),
                           [key](const Value& item) { return item.asObject().at(key).asString(); });
    return ids;
}

TypeDescriptor integerType() {
    return {.kind = TypeDescriptor::Kind::Integer,
            .nullable = false,
            .description = "count",
            .element_type = {},
            .fields = {},
            .value_type = {}};
}

} // namespace

TEST(CommandDispatcher, RejectsUnknownAndEmptyMethods) {
    const CommandFixture fixture;
    expectError(fixture.dispatcher.dispatch("", {}, {}), ErrorCode::UnknownCommand, std::nullopt);
    expectError(fixture.dispatcher.dispatch("action.LIST", {}, {}), ErrorCode::UnknownCommand,
                std::nullopt);
    expectError(fixture.dispatcher.dispatch("nope", {}, {}), ErrorCode::UnknownCommand,
                std::nullopt);
    const auto unknown = fixture.dispatcher.dispatch("nope", {}, {});
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().message, "Unknown command: nope");
    EXPECT_FALSE(unknown.error().details.has_value());
}

TEST(CommandDispatcher, ReportsMissingFieldsBeforeUnknownFields) {
    const CommandFixture fixture;
    expectError(fixture.dispatcher.dispatch(action_describe, object({{"extra", Value{true}}}), {}),
                ErrorCode::MissingArgument, "action");
    expectError(
        fixture.dispatcher.dispatch(action_invoke, object({{"action", Value{"math.ping"}}}), {}),
        ErrorCode::MissingArgument, "arguments");
}

TEST(CommandDispatcher, ReportsUnknownFieldsInDictionaryOrder) {
    const CommandFixture fixture;
    const auto result = fixture.dispatcher.dispatch(
        action_describe,
        object({{"action", Value{"math.ping"}}, {"zzz", Value{1}}, {"aaa", Value{2}}}), {});
    expectError(result, ErrorCode::UnknownArgument, "aaa");
}

TEST(CommandDispatcher, EncodesSpecialUnknownFieldPaths) {
    const CommandFixture fixture;
    expectError(fixture.dispatcher.dispatch(action_list, object({{"a.b", Value{"x"}}}), {}),
                ErrorCode::UnknownArgument, "[\"a.b\"]");
    expectError(fixture.dispatcher.dispatch(system_snapshot, object({{"", Value{true}}}), {}),
                ErrorCode::UnknownArgument, "[\"\"]");
}

TEST(CommandDispatcher, RejectsWrongTypesForStringCommandFields) {
    const CommandFixture fixture;
    expectTypeMismatch(fixture.dispatcher, action_describe, {}, "action");
    expectTypeMismatch(fixture.dispatcher, action_list, {}, "module");
    expectTypeMismatch(fixture.dispatcher, resource_list, {}, "type");
    expectTypeMismatch(fixture.dispatcher, task_list, {}, "state");
}

TEST(CommandDispatcher, RejectsWrongTypesForArgumentsAndTags) {
    const CommandFixture fixture;
    for(const auto& wrong : nonMatchingTypes(Value::Type::Object)) {
        expectError(
            fixture.dispatcher.dispatch(
                action_invoke, object({{"action", Value{"math.ping"}}, {"arguments", wrong}}), {}),
            ErrorCode::TypeMismatch, "arguments");
    }
    for(const auto& wrong : nonMatchingTypes(Value::Type::Array)) {
        expectError(fixture.dispatcher.dispatch(action_list, object({{"tags", wrong}}), {}),
                    ErrorCode::TypeMismatch, "tags");
    }
}

TEST(CommandDispatcher, ReportsNonStringTagElementPaths) {
    const CommandFixture fixture;
    const auto result = fixture.dispatcher.dispatch(
        action_list, object({{"tags", Value{Value::Array{Value{"ok"}, Value{true}}}}}), {});
    expectError(result, ErrorCode::TypeMismatch, "tags[1]");
}

TEST(CommandDispatcher, RejectsNonCanonicalIdentifiers) {
    const CommandFixture fixture;
    expectError(
        fixture.dispatcher.dispatch(action_describe, object({{"action", Value{"Math"}}}), {}),
        ErrorCode::InvalidArgument, "action");
    expectError(
        fixture.dispatcher.dispatch(resource_describe, object({{"resource", Value{"widget"}}}), {}),
        ErrorCode::InvalidArgument, "resource");
    expectError(fixture.dispatcher.dispatch(task_describe, object({{"task", Value{"task"}}}), {}),
                ErrorCode::InvalidArgument, "task");
}

TEST(CommandDispatcher, RejectsIllegalTaskStateAndResourceType) {
    const CommandFixture fixture;
    expectError(fixture.dispatcher.dispatch(task_list, object({{"state", Value{"RUNNING"}}}), {}),
                ErrorCode::InvalidArgument, "state");
    expectError(fixture.dispatcher.dispatch(resource_list, object({{"type", Value{"Widget"}}}), {}),
                ErrorCode::InvalidArgument, "type");
}

TEST(CommandDispatcher, ValidationFailureDoesNotInvokeOrCancel) {
    CommandFixture fixture;
    axiom::async::Executor executor{1};
    std::promise<void> release;
    auto blocker = executor.submit([wait = release.get_future()] { wait.wait(); });
    auto submitted = fixture.tasks.submit(executor, "blocked", [](axiom::task::TaskContext&) {
        return axiom::Result<int>::success(1);
    });
    ASSERT_TRUE(submitted);
    const auto failed_invoke =
        fixture.dispatcher.dispatch(action_invoke, object({{"action", Value{"math.probe"}}}), {});
    const auto failed_cancel =
        fixture.dispatcher.dispatch(task_cancel, object({{"task", Value{"nope"}}}), {});
    EXPECT_EQ(fixture.probe.invokes.load(), 0);
    EXPECT_EQ(submitted.value().state(), axiom::task::TaskState::Pending);
    expectError(failed_invoke, ErrorCode::MissingArgument, "arguments");
    expectError(failed_cancel, ErrorCode::InvalidArgument, "task");
    release.set_value();
    executor.close();
    blocker.get();
}

TEST(CommandDispatcher, ListsAndFiltersActions) {
    const CommandFixture fixture;
    const auto all = fixture.dispatcher.dispatch(action_list, {}, {});
    ASSERT_TRUE(all);
    const auto module =
        fixture.dispatcher.dispatch(action_list, object({{"module", Value{"math"}}}), {});
    ASSERT_TRUE(module);
    const auto tags = fixture.dispatcher.dispatch(
        action_list, object({{"tags", Value{Value::Array{Value{"search"}}}}}), {});
    ASSERT_TRUE(tags);
    const auto none = fixture.dispatcher.dispatch(
        action_list,
        object({{"module", Value{"missing"}}, {"tags", Value{Value::Array{Value{"search"}}}}}), {});
    ASSERT_TRUE(none);
    EXPECT_TRUE(none.value().asArray().empty());
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    std::vector<std::string> expected;
    const auto actions = service.actions();
    expected.reserve(actions.size());
    std::ranges::transform(actions, std::back_inserter(expected),
                           [](const auto& action) { return std::string{action.id.str()}; });
    EXPECT_EQ(stringIds(all.value(), "id"), expected);
    EXPECT_EQ(stringIds(module.value(), "id").size(), service.actions("math").size());
    EXPECT_EQ(stringIds(tags.value(), "id").size(),
              service.actions(axiom::introspection::ActionQuery{.module = {}, .tags = {"search"}})
                  .size());
}

TEST(CommandDispatcher, DescribesActionWithNestedTypesAndOmitsEmptyVersion) {
    const CommandFixture fixture;
    const auto described = fixture.dispatcher.dispatch(
        action_describe, object({{"action", Value{"catalog.inspect"}}}), {});
    ASSERT_TRUE(described);
    const auto& encoded = described.value().asObject();
    EXPECT_EQ(encoded.at("id").asString(), "catalog.inspect");
    EXPECT_FALSE(encoded.contains("version"));
    const auto& values_type = encoded.at("parameters").asArray().front().asObject().at("type");
    EXPECT_EQ(values_type.asObject().at("kind").asString(), "array");
    EXPECT_EQ(values_type.asObject().at("element_type").asObject().at("kind").asString(), "array");
    const auto& fields_type = encoded.at("parameters").asArray().at(1).asObject().at("type");
    EXPECT_EQ(fields_type.asObject().at("kind").asString(), "object");
    EXPECT_TRUE(fields_type.asObject().contains("value_type"));
    EXPECT_FALSE(fields_type.asObject().contains("fields"));
}

TEST(CommandDispatcher, InvokesActionsAndForwardsContextUnchanged) {
    const CommandFixture fixture;
    const InvocationContext context{.request_id = "req",
                                    .trace_id = "trace",
                                    .caller = "adapter",
                                    .metadata = {{"source", "test"}}};
    const auto ping = fixture.dispatcher.dispatch(
        action_invoke, object({{"action", Value{"math.ping"}}, {"arguments", Value{object({})}}}),
        context);
    ASSERT_TRUE(ping);
    EXPECT_EQ(ping.value().asInteger(), 1);
    const auto probed = fixture.dispatcher.dispatch(
        action_invoke, object({{"action", Value{"math.probe"}}, {"arguments", Value{object({})}}}),
        context);
    ASSERT_TRUE(probed);
    EXPECT_EQ(probed.value().asInteger(), 7);
    EXPECT_EQ(fixture.probe.last_context.request_id, "req");
    EXPECT_EQ(fixture.probe.last_context.trace_id, "trace");
    EXPECT_EQ(fixture.probe.last_context.caller, "adapter");
    EXPECT_EQ(fixture.probe.last_context.metadata.at("source"), "test");
}

TEST(CommandDispatcher, PreservesRuntimeArgumentAndBusinessErrors) {
    const CommandFixture fixture;
    const auto missing = fixture.dispatcher.dispatch(
        action_invoke, object({{"action", Value{"math.add"}}, {"arguments", Value{object({})}}}),
        {});
    expectError(missing, ErrorCode::MissingArgument, "left");
    const auto extra = fixture.dispatcher.dispatch(
        action_invoke,
        object({{"action", Value{"math.add"}},
                {"arguments",
                 Value{object({{"left", Value{1}}, {"right", Value{2}}, {"bonus", Value{3}}})}}}),
        {});
    expectError(extra, ErrorCode::UnknownArgument, "bonus");
    const auto mismatch = fixture.dispatcher.dispatch(
        action_invoke,
        object({{"action", Value{"math.add"}},
                {"arguments", Value{object({{"left", Value{"x"}}, {"right", Value{2}}})}}}),
        {});
    expectError(mismatch, ErrorCode::TypeMismatch, "left");
    const auto business = fixture.dispatcher.dispatch(
        action_invoke, object({{"action", Value{"math.fail"}}, {"arguments", Value{object({})}}}),
        {});
    expectError(business, ErrorCode::InvalidArgument, "shape.size");
}

TEST(CommandDispatcher, PreservesRuntimeExceptionNormalization) {
    const CommandFixture fixture;
    const auto standard = fixture.dispatcher.dispatch(
        action_invoke, object({{"action", Value{"math.boom"}}, {"arguments", Value{object({})}}}),
        {});
    expectError(standard, ErrorCode::InvocationFailed, std::nullopt);
    const auto unknown = fixture.dispatcher.dispatch(
        action_invoke,
        object({{"action", Value{"math.explode"}}, {"arguments", Value{object({})}}}), {});
    expectError(unknown, ErrorCode::InternalError, std::nullopt);
    const auto missing = fixture.dispatcher.dispatch(
        action_describe, object({{"action", Value{"math.missing"}}}), {});
    expectError(missing, ErrorCode::NotFound, std::nullopt);
}

TEST(CommandDispatcher, ListsAndDescribesResources) {
    CommandFixture fixture;
    const auto empty = fixture.dispatcher.dispatch(resource_list, {}, {});
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty.value().asArray().empty());
    const auto added = fixture.resources.add(std::make_unique<Widget>());
    ASSERT_TRUE(added);
    const auto listed = fixture.dispatcher.dispatch(resource_list, {}, {});
    ASSERT_TRUE(listed);
    ASSERT_EQ(listed.value().asArray().size(), 1U);
    EXPECT_EQ(listed.value().asArray().front().asObject().at("type").asString(), "widget");
    EXPECT_EQ(listed.value().asArray().front().asObject().size(), 2U);
    const auto described = fixture.dispatcher.dispatch(
        resource_describe, object({{"resource", Value{std::string{added.value().id().str()}}}}),
        {});
    ASSERT_TRUE(described);
    EXPECT_EQ(described.value().asObject().at("id").asString(), added.value().id().str());
    ASSERT_TRUE(fixture.resources.remove(added.value().id()));
    expectError(fixture.dispatcher.dispatch(
                    resource_describe,
                    object({{"resource", Value{std::string{added.value().id().str()}}}}), {}),
                ErrorCode::NotFound, std::nullopt);
}

TEST(CommandDispatcher, FiltersResourcesAndPreservesLegalEmptyMatches) {
    CommandFixture fixture;
    ASSERT_TRUE(fixture.resources.add(std::make_unique<Widget>()));
    const auto matched =
        fixture.dispatcher.dispatch(resource_list, object({{"type", Value{"widget"}}}), {});
    ASSERT_TRUE(matched);
    EXPECT_EQ(matched.value().asArray().size(), 1U);
    const auto none =
        fixture.dispatcher.dispatch(resource_list, object({{"type", Value{"geometry"}}}), {});
    ASSERT_TRUE(none);
    EXPECT_TRUE(none.value().asArray().empty());
}

TEST(CommandDispatcher, EncodesTaskDescriptorsAndCancelsKnownTasks) {
    CommandFixture fixture;
    axiom::async::Executor executor{1};
    auto completed = fixture.tasks.submit(
        executor,
        axiom::task::TaskSubmission{.name = "done",
                                    .origin = axiom::task::TaskOrigin{.request_id = "req",
                                                                      .trace_id = "trace",
                                                                      .caller = "suite",
                                                                      .action_id = "math.ping",
                                                                      .metadata = {{"k", "v"}}}},
        [](axiom::task::TaskContext&) { return axiom::Result<int>::success(1); });
    ASSERT_TRUE(completed);
    executor.close();
    const auto listed = fixture.dispatcher.dispatch(task_list, {}, {});
    ASSERT_TRUE(listed);
    ASSERT_EQ(listed.value().asArray().size(), 1U);
    const auto& encoded = listed.value().asArray().front().asObject();
    EXPECT_EQ(encoded.at("state").asString(), "completed");
    EXPECT_EQ(encoded.at("origin").asObject().at("action_id").asString(), "math.ping");
    EXPECT_FALSE(encoded.contains("error"));
    const auto cancelled = fixture.dispatcher.dispatch(
        task_cancel, object({{"task", Value{std::string{completed.value().id().str()}}}}), {});
    ASSERT_TRUE(cancelled);
    EXPECT_TRUE(cancelled.value().isNull());
}

TEST(CommandDispatcher, EncodesFailedTaskErrorAndEmptyOriginFields) {
    CommandFixture fixture;
    axiom::async::Executor executor{1};
    auto failed = fixture.tasks.submit(
        executor,
        axiom::task::TaskSubmission{.name = "fail",
                                    .origin = axiom::task::TaskOrigin{.request_id = "",
                                                                      .trace_id = "",
                                                                      .caller = "",
                                                                      .action_id = "math.fail",
                                                                      .metadata = {}}},
        [](axiom::task::TaskContext&) {
            return axiom::Result<int>::failure(
                {.code = ErrorCode::InvalidArgument,
                 .message = "bad item",
                 .path = "items[1]",
                 .details = Value{object({{"expected", Value{"integer"}}})}});
        });
    ASSERT_TRUE(failed);
    executor.close();
    const auto described = fixture.dispatcher.dispatch(
        task_describe, object({{"task", Value{std::string{failed.value().id().str()}}}}), {});
    ASSERT_TRUE(described);
    const auto& encoded = described.value().asObject();
    EXPECT_EQ(encoded.at("state").asString(), "failed");
    EXPECT_EQ(encoded.at("error").asObject().at("path").asString(), "items[1]");
    EXPECT_EQ(encoded.at("error").asObject().at("code").asString(), "invalid_argument");
    EXPECT_TRUE(encoded.at("origin").asObject().at("request_id").asString().empty());
    const auto filtered = fixture.dispatcher.dispatch(
        task_list, object({{"state", Value{"failed"}}, {"origin_action", Value{"math.fail"}}}), {});
    ASSERT_TRUE(filtered);
    EXPECT_EQ(filtered.value().asArray().size(), 1U);
}

TEST(CommandDispatcher, SnapshotUsesFourFixedKeysAndStaysIndependent) {
    CommandFixture fixture;
    const auto first = fixture.dispatcher.dispatch(system_snapshot, {}, {});
    ASSERT_TRUE(first);
    const auto keys = first.value().asObject();
    ASSERT_EQ(keys.size(), 4U);
    EXPECT_TRUE(keys.contains("actions"));
    EXPECT_TRUE(keys.contains("modules"));
    EXPECT_TRUE(keys.contains("resources"));
    EXPECT_TRUE(keys.contains("tasks"));
    ModuleBuilder extra{
        {.namespace_name = "later", .description = {}, .version = {}, .tags = {}, .metadata = {}}};
    ASSERT_TRUE(extra.add("nudge", "Nudge", [] { return 0; }));
    ASSERT_TRUE(fixture.runtime.registerModule(std::move(extra)));
    EXPECT_EQ(first.value().asObject().at("modules").asArray().size(), 2U);
    const auto second = fixture.dispatcher.dispatch(system_snapshot, {}, {});
    ASSERT_TRUE(second);
    EXPECT_EQ(second.value().asObject().at("modules").asArray().size(), 3U);
}

TEST(CommandDispatcher, DestructorDoesNotAffectSources) {
    ProbeState probe;
    Runtime runtime;
    const axiom::resource::ResourceRegistry resources;
    axiom::task::TaskRegistry tasks;
    registerCatalog(runtime, probe);
    {
        const CommandDispatcher dispatcher{runtime, resources, tasks};
        ASSERT_TRUE(dispatcher.dispatch(action_list, {}, {}));
    }
    const auto id = ActionId::parse("math.ping");
    ASSERT_TRUE(id);
    const auto invoked = runtime.invoke(id.value(), {}, {});
    ASSERT_TRUE(invoked);
    EXPECT_EQ(invoked.value().asInteger(), 1);
}

TEST(CommandDispatcher, SupportsConcurrentDispatchWithoutSleep) {
    CommandFixture fixture;
    constexpr int workers = 4;
    axiom::async::Executor executor{1};
    std::vector<std::string> task_ids;
    task_ids.reserve(workers);
    for(int index = 0; index < workers; ++index) {
        auto submitted =
            fixture.tasks.submit(executor, "concurrent", [](axiom::task::TaskContext&) {
                return axiom::Result<void>::success();
            });
        ASSERT_TRUE(submitted);
        task_ids.emplace_back(submitted.value().id().str());
    }
    executor.close();
    std::latch start{workers};
    std::atomic<int> successes{0};
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for(int index = 0; index < workers; ++index) {
        threads.emplace_back([&fixture, &start, &successes, &task_ids, index] {
            start.arrive_and_wait();
            const auto listed = fixture.dispatcher.dispatch(action_list, {}, {});
            const auto described = fixture.dispatcher.dispatch(
                action_describe, object({{"action", Value{"math.ping"}}}), {});
            const auto invoked = fixture.dispatcher.dispatch(
                action_invoke,
                object({{"action", Value{"math.ping"}}, {"arguments", Value{object({})}}}), {});
            const auto snapshot = fixture.dispatcher.dispatch(system_snapshot, {}, {});
            const auto cancelled = fixture.dispatcher.dispatch(
                task_cancel, object({{"task", Value{task_ids[static_cast<std::size_t>(index)]}}}),
                {});
            if(listed && described && invoked && snapshot && cancelled &&
               cancelled.value().isNull()) {
                ++successes;
            }
        });
    }
    for(auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(successes.load(), workers);
}

TEST(CommandConversion, EncodesEveryErrorCodeName) {
    using axiom::command::detail::errorCodeName;
    EXPECT_EQ(errorCodeName(ErrorCode::InvalidArgument), "invalid_argument");
    EXPECT_EQ(errorCodeName(ErrorCode::MissingArgument), "missing_argument");
    EXPECT_EQ(errorCodeName(ErrorCode::UnknownArgument), "unknown_argument");
    EXPECT_EQ(errorCodeName(ErrorCode::TypeMismatch), "type_mismatch");
    EXPECT_EQ(errorCodeName(ErrorCode::NotFound), "not_found");
    EXPECT_EQ(errorCodeName(ErrorCode::AlreadyExists), "already_exists");
    EXPECT_EQ(errorCodeName(ErrorCode::InvalidDescriptor), "invalid_descriptor");
    EXPECT_EQ(errorCodeName(ErrorCode::InvocationFailed), "invocation_failed");
    EXPECT_EQ(errorCodeName(ErrorCode::InternalError), "internal_error");
    EXPECT_EQ(errorCodeName(ErrorCode::Cancelled), "cancelled");
    EXPECT_EQ(errorCodeName(ErrorCode::UnknownCommand), "unknown_command");
}

TEST(CommandConversion, RejectsUnknownEnumerators) {
    EXPECT_THROW(
        static_cast<void>(axiom::command::detail::errorCodeName(static_cast<ErrorCode>(255))),
        std::logic_error);
    EXPECT_THROW(static_cast<void>(axiom::command::detail::taskStateName(
                     static_cast<axiom::task::TaskState>(255))),
                 std::logic_error);
}

TEST(CommandConversion, EncodesRemainingTypeKindsAndTaskStates) {
    using axiom::command::detail::encodeTypeDescriptor;
    using axiom::command::detail::taskStateName;
    EXPECT_EQ(encodeTypeDescriptor({.kind = TypeDescriptor::Kind::Null,
                                    .nullable = false,
                                    .description = {},
                                    .element_type = {},
                                    .fields = {},
                                    .value_type = {}})
                  .asObject()
                  .at("kind")
                  .asString(),
              "null");
    EXPECT_EQ(encodeTypeDescriptor({.kind = TypeDescriptor::Kind::Boolean,
                                    .nullable = false,
                                    .description = {},
                                    .element_type = {},
                                    .fields = {},
                                    .value_type = {}})
                  .asObject()
                  .at("kind")
                  .asString(),
              "boolean");
    EXPECT_EQ(encodeTypeDescriptor({.kind = TypeDescriptor::Kind::Number,
                                    .nullable = false,
                                    .description = {},
                                    .element_type = {},
                                    .fields = {},
                                    .value_type = {}})
                  .asObject()
                  .at("kind")
                  .asString(),
              "number");
    EXPECT_EQ(encodeTypeDescriptor({.kind = TypeDescriptor::Kind::String,
                                    .nullable = false,
                                    .description = {},
                                    .element_type = {},
                                    .fields = {},
                                    .value_type = {}})
                  .asObject()
                  .at("kind")
                  .asString(),
              "string");
    EXPECT_EQ(taskStateName(axiom::task::TaskState::Running), "running");
    EXPECT_EQ(taskStateName(axiom::task::TaskState::Completed), "completed");
    EXPECT_EQ(taskStateName(axiom::task::TaskState::Failed), "failed");
}

TEST(CommandDispatcher, AcceptsEveryCanonicalTaskStateFilter) {
    const CommandFixture fixture;
    for(const char* state : {"pending", "running", "completed", "failed", "cancelled"}) {
        const auto listed =
            fixture.dispatcher.dispatch(task_list, object({{"state", Value{state}}}), {});
        ASSERT_TRUE(listed) << state;
        EXPECT_TRUE(listed.value().isArray());
    }
}

TEST(CommandDispatcher, FiltersTasksByOriginRequestAndCombinations) {
    CommandFixture fixture;
    axiom::async::Executor executor{1};
    auto ping_a = fixture.tasks.submit(
        executor,
        axiom::task::TaskSubmission{.name = "ping-a", .origin = testOrigin("req-a", "math.ping")},
        [](axiom::task::TaskContext&) { return axiom::Result<void>::success(); });
    auto ping_b = fixture.tasks.submit(
        executor,
        axiom::task::TaskSubmission{.name = "ping-b", .origin = testOrigin("req-b", "math.ping")},
        [](axiom::task::TaskContext&) { return axiom::Result<void>::success(); });
    auto fail_a = fixture.tasks.submit(
        executor,
        axiom::task::TaskSubmission{.name = "fail-a", .origin = testOrigin("req-a", "math.fail")},
        [](axiom::task::TaskContext&) { return axiom::Result<void>::success(); });
    ASSERT_TRUE(ping_a);
    ASSERT_TRUE(ping_b);
    ASSERT_TRUE(fail_a);
    executor.close();
    const auto by_request =
        fixture.dispatcher.dispatch(task_list, object({{"origin_request", Value{"req-a"}}}), {});
    ASSERT_TRUE(by_request);
    EXPECT_EQ(stringIds(by_request.value(), "id"),
              (std::vector<std::string>{std::string{ping_a.value().id().str()},
                                        std::string{fail_a.value().id().str()}}));
    const auto by_action =
        fixture.dispatcher.dispatch(task_list, object({{"origin_action", Value{"math.ping"}}}), {});
    ASSERT_TRUE(by_action);
    EXPECT_EQ(stringIds(by_action.value(), "id"),
              (std::vector<std::string>{std::string{ping_a.value().id().str()},
                                        std::string{ping_b.value().id().str()}}));
    const auto combined = fixture.dispatcher.dispatch(
        task_list,
        object({{"origin_action", Value{"math.ping"}}, {"origin_request", Value{"req-a"}}}), {});
    ASSERT_TRUE(combined);
    EXPECT_EQ(stringIds(combined.value(), "id"),
              std::vector<std::string>{std::string{ping_a.value().id().str()}});
    const auto none =
        fixture.dispatcher.dispatch(task_list, object({{"origin_request", Value{"missing"}}}), {});
    ASSERT_TRUE(none);
    EXPECT_TRUE(none.value().asArray().empty());
}

TEST(CommandDispatcher, CancelsActiveTaskAndPreservesUnknownNotFound) {
    CommandFixture fixture;
    axiom::async::Executor executor{1};
    std::promise<void> release;
    auto blocker = executor.submit([wait = release.get_future()] { wait.wait(); });
    auto pending = fixture.tasks.submit(
        executor,
        axiom::task::TaskSubmission{.name = "pending", .origin = testOrigin("req", "math.ping")},
        [](axiom::task::TaskContext&) { return axiom::Result<int>::success(1); });
    ASSERT_TRUE(pending);
    EXPECT_EQ(pending.value().state(), axiom::task::TaskState::Pending);
    const auto cancelled = fixture.dispatcher.dispatch(
        task_cancel, object({{"task", Value{std::string{pending.value().id().str()}}}}), {});
    ASSERT_TRUE(cancelled);
    EXPECT_TRUE(cancelled.value().isNull());
    EXPECT_EQ(pending.value().state(), axiom::task::TaskState::Cancelled);
    expectError(
        fixture.dispatcher.dispatch(task_cancel, object({{"task", Value{"task:999999"}}}), {}),
        ErrorCode::NotFound, std::nullopt);
    expectError(
        fixture.dispatcher.dispatch(task_describe, object({{"task", Value{"task:999999"}}}), {}),
        ErrorCode::NotFound, std::nullopt);
    release.set_value();
    executor.close();
    blocker.get();
}

TEST(CommandDispatcher, OmitsUnknownTaskOrigin) {
    CommandFixture fixture;
    axiom::async::Executor executor{1};
    auto submitted = fixture.tasks.submit(
        executor, "anon", [](axiom::task::TaskContext&) { return axiom::Result<void>::success(); });
    ASSERT_TRUE(submitted);
    executor.close();
    const auto described = fixture.dispatcher.dispatch(
        task_describe, object({{"task", Value{std::string{submitted.value().id().str()}}}}), {});
    ASSERT_TRUE(described);
    EXPECT_FALSE(described.value().asObject().contains("origin"));
}

TEST(CommandConversion, RejectsImpossibleObjectShapes) {
    auto both = TypeDescriptor::object({{"size", TypeDescriptor::nested(integerType())}});
    both.value_type = TypeDescriptor::nested(integerType());
    EXPECT_THROW(static_cast<void>(axiom::command::detail::encodeTypeDescriptor(both)),
                 std::logic_error);
    TypeDescriptor missing_field = TypeDescriptor::object({});
    missing_field.fields.emplace("size", std::shared_ptr<const TypeDescriptor>{});
    EXPECT_THROW(static_cast<void>(axiom::command::detail::encodeTypeDescriptor(missing_field)),
                 std::logic_error);
    EXPECT_THROW(static_cast<void>(axiom::command::detail::encodeTypeDescriptor(
                     TypeDescriptor{.kind = static_cast<TypeDescriptor::Kind>(255),
                                    .nullable = false,
                                    .description = {},
                                    .element_type = {},
                                    .fields = {},
                                    .value_type = {}})),
                 std::logic_error);
}

TEST(CommandConversion, EncodesFixedObjectNullableAndNullDefault) {
    const auto nested = integerType();
    const auto fixed =
        TypeDescriptor::object({{"size", TypeDescriptor::nested(nested)}}, "shape", false);
    const auto encoded_fixed = axiom::command::detail::encodeTypeDescriptor(fixed);
    EXPECT_TRUE(encoded_fixed.asObject().contains("fields"));
    EXPECT_TRUE(encoded_fixed.asObject().at("fields").asObject().contains("size"));
    const auto empty_object = TypeDescriptor::object({}, "empty");
    EXPECT_TRUE(axiom::command::detail::encodeTypeDescriptor(empty_object)
                    .asObject()
                    .at("fields")
                    .asObject()
                    .empty());
    TypeDescriptor nullable = integerType();
    nullable.nullable = true;
    EXPECT_TRUE(axiom::command::detail::encodeTypeDescriptor(nullable)
                    .asObject()
                    .at("nullable")
                    .asBoolean());
    const ParameterDescriptor parameter{.name = "label",
                                        .description = "Label",
                                        .required = false,
                                        .type = nullable,
                                        .default_value = Value{nullptr}};
    const auto encoded = axiom::command::detail::encodeParameterDescriptor(parameter);
    EXPECT_TRUE(encoded.asObject().contains("default"));
    EXPECT_TRUE(encoded.asObject().at("default").isNull());
}

TEST(CommandConversion, EncodesErrorDetailsAndRejectsImpossibleTypes) {
    const Error error{.code = ErrorCode::TypeMismatch,
                      .message = "bad",
                      .path = "items[0]",
                      .details = Value{object({{"expected", Value{"integer"}}})}};
    const auto encoded = axiom::command::detail::encodeError(error);
    EXPECT_EQ(encoded.asObject().at("code").asString(), "type_mismatch");
    EXPECT_EQ(encoded.asObject().at("details").asObject().at("expected").asString(), "integer");
    TypeDescriptor broken_array{.kind = TypeDescriptor::Kind::Array,
                                .nullable = false,
                                .description = {},
                                .element_type = {},
                                .fields = {},
                                .value_type = {}};
    EXPECT_THROW(static_cast<void>(axiom::command::detail::encodeTypeDescriptor(broken_array)),
                 std::logic_error);
}

TEST(CommandValidation, CoversHelperEdgePaths) {
    using axiom::command::detail::commandFields;
    using axiom::command::detail::CommandMethod;
    using axiom::command::detail::readRequiredString;
    using axiom::command::detail::withDefaultPath;
    EXPECT_TRUE(commandFields(CommandMethod::SystemSnapshot).empty());
    EXPECT_THROW(static_cast<void>(commandFields(static_cast<CommandMethod>(99))),
                 std::logic_error);
    const auto missing = readRequiredString({}, "action");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, ErrorCode::TypeMismatch);
    Error sourced{.code = ErrorCode::InvalidArgument,
                  .message = "kept",
                  .path = "inner",
                  .details = std::nullopt};
    EXPECT_EQ(withDefaultPath(sourced, "action").path, "inner");
}
