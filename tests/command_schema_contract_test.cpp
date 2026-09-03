#include "command/descriptor_conversion.hpp"

#include <axiom/action/module_builder.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/async/executor.hpp>
#include <axiom/command/command_dispatcher.hpp>
#include <axiom/command/command_methods.hpp>
#include <axiom/command/error_code_name.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/resource/resource_traits.hpp>
#include <axiom/task/task_registry.hpp>
#include <axiom/task/task_types.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Widget final {};

} // namespace

template <> struct axiom::resource::ResourceTraits<::Widget> {
    static constexpr std::string_view type_name = "widget";
};

namespace {

using axiom::Error;
using axiom::ErrorCode;
using axiom::ModuleBuilder;
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
using axiom::resource::ResourceRegistry;
using axiom::task::TaskRegistry;

[[nodiscard]] Value::Object object(std::initializer_list<Value::Object::value_type> fields) {
    return Value::Object{fields};
}

[[nodiscard]] Value stringType() {
    return Value{object(
        {{"description", Value{""}}, {"kind", Value{"string"}}, {"nullable", Value{false}}})};
}

[[nodiscard]] Value integerType() {
    return Value{object(
        {{"description", Value{""}}, {"kind", Value{"integer"}}, {"nullable", Value{false}}})};
}

[[nodiscard]] Value echoActionValue() {
    Value label_parameter{object({
        {"default", Value{"none"}},
        {"description", Value{"Label"}},
        {"name", Value{"label"}},
        {"required", Value{false}},
        {"type", stringType()},
    })};
    Value count_parameter{object({
        {"description", Value{"Repetitions"}},
        {"name", Value{"count"}},
        {"required", Value{true}},
        {"type", integerType()},
    })};
    Value::Array parameters;
    parameters.reserve(2);
    parameters.push_back(std::move(label_parameter));
    parameters.push_back(std::move(count_parameter));
    return Value{object({{"description", Value{"Echo once"}},
                         {"id", Value{"schema.echo"}},
                         {"metadata", Value{object({{"kind", Value{"echo"}}})}},
                         {"parameters", Value{std::move(parameters)}},
                         {"return_type", integerType()},
                         {"tags", Value{Value::Array{Value{"read"}}}},
                         {"version", Value{"2"}}})};
}

[[nodiscard]] Value resourceValue(const std::string& identifier) {
    return Value{object({{"id", Value{identifier}}, {"type", Value{"widget"}}})};
}

[[nodiscard]] Value taskValue(const std::string& identifier) {
    return Value{
        object({{"id", Value{identifier}},
                {"name", Value{"sequence"}},
                {"origin", Value{object({{"action_id", Value{"schema.echo"}},
                                         {"caller", Value{"suite"}},
                                         {"metadata", Value{object({{"k", Value{"v"}}})}},
                                         {"request_id", Value{"req"}},
                                         {"trace_id", Value{"trace"}}})}},
                {"progress", Value{object({{"message", Value{""}}, {"value", Value{1.0}}})}},
                {"state", Value{"completed"}}})};
}

// NOLINTBEGIN(misc-no-recursion): the comparator mirrors the recursive Value shape.
[[nodiscard]] bool valueEquals(const Value& actual, const Value& expected);
[[nodiscard]] std::string renderValue(const Value& value);

[[nodiscard]] bool arrayEquals(const Value::Array& actual, const Value::Array& expected) {
    if(actual.size() != expected.size()) {
        return false;
    }
    for(std::size_t index = 0; index < actual.size(); ++index) {
        if(!valueEquals(actual[index], expected[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool objectEquals(const Value::Object& actual, const Value::Object& expected) {
    if(actual.size() != expected.size()) {
        return false;
    }
    return std::ranges::all_of(actual, [&expected](const auto& entry) {
        const auto found = expected.find(entry.first);
        return found != expected.end() && valueEquals(entry.second, found->second);
    });
}

[[nodiscard]] bool valueEquals(const Value& actual, const Value& expected) {
    if(actual.type() != expected.type()) {
        return false;
    }
    switch(actual.type()) {
    case Value::Type::Null:
        return true;
    case Value::Type::Boolean:
        return actual.asBoolean() == expected.asBoolean();
    case Value::Type::Integer:
        return actual.asInteger() == expected.asInteger();
    case Value::Type::Number:
        return actual.asNumber() == expected.asNumber();
    case Value::Type::String:
        return actual.asString() == expected.asString();
    case Value::Type::Array:
        return arrayEquals(actual.asArray(), expected.asArray());
    case Value::Type::Object:
        return objectEquals(actual.asObject(), expected.asObject());
    }
    return false;
}

[[nodiscard]] std::string renderArray(const Value::Array& values) {
    std::string rendered = "[";
    for(auto iterator = values.begin(); iterator != values.end(); ++iterator) {
        if(iterator != values.begin()) {
            rendered += ", ";
        }
        rendered += renderValue(*iterator);
    }
    return rendered + "]";
}

[[nodiscard]] std::string renderObject(const Value::Object& fields) {
    std::string rendered = "{";
    for(auto iterator = fields.begin(); iterator != fields.end(); ++iterator) {
        if(iterator != fields.begin()) {
            rendered += ", ";
        }
        rendered += "\"" + iterator->first + "\": " + renderValue(iterator->second);
    }
    return rendered + "}";
}

[[nodiscard]] std::string renderValue(const Value& value) {
    switch(value.type()) {
    case Value::Type::Null:
        return "null";
    case Value::Type::Boolean:
        return value.asBoolean() ? "true" : "false";
    case Value::Type::Integer:
        return std::to_string(value.asInteger());
    case Value::Type::Number:
        return std::to_string(value.asNumber());
    case Value::Type::String:
        return "\"" + value.asString() + "\"";
    case Value::Type::Array:
        return renderArray(value.asArray());
    case Value::Type::Object:
        return renderObject(value.asObject());
    }
    return "<unknown>";
}
// NOLINTEND(misc-no-recursion)

void expectWholeValue(const Value& actual, const Value& expected) {
    EXPECT_TRUE(valueEquals(actual, expected))
        << "actual: " << renderValue(actual) << "\nexpected: " << renderValue(expected);
}

void expectWholeError(const Error& actual, const Error& expected) {
    EXPECT_EQ(actual.code, expected.code);
    EXPECT_EQ(actual.message, expected.message);
    EXPECT_EQ(actual.path, expected.path);
    ASSERT_EQ(actual.details.has_value(), expected.details.has_value());
    if(actual.details.has_value() && expected.details.has_value()) {
        expectWholeValue(*actual.details, *expected.details);
    }
}

[[nodiscard]] axiom::task::TaskOrigin schemaOrigin() {
    return {.request_id = "req",
            .trace_id = "trace",
            .caller = "suite",
            .action_id = "schema.echo",
            .metadata = {{"k", "v"}}};
}

struct SchemaWorld {
    SchemaWorld() {
        ModuleBuilder schema{{.namespace_name = "schema",
                              .description = "Schema module",
                              .version = "1.0",
                              .tags = {"core"},
                              .metadata = {{"owner", "tests"}}}};
        EXPECT_TRUE(schema.add(
            "echo", "Echo once",
            [](const std::string& label, const int count) {
                return static_cast<int>(label.size()) + count;
            },
            axiom::ActionOptions{.version = "2", .tags = {"read"}, .metadata = {{"kind", "echo"}}},
            axiom::param("label", "Label", Value{"none"}), axiom::param("count", "Repetitions")));
        EXPECT_TRUE(runtime.registerModule(std::move(schema)));
        const auto resource = resources.add(std::make_unique<Widget>());
        EXPECT_TRUE(resource);
        if(resource) {
            resource_id = std::string{resource.value().id().str()};
        }
        auto submitted = tasks.submit(
            executor, axiom::task::TaskSubmission{.name = "sequence", .origin = schemaOrigin()},
            [](axiom::task::TaskContext&) { return axiom::Result<int>::success(1); });
        EXPECT_TRUE(submitted);
        if(submitted) {
            task_id = std::string{submitted.value().id().str()};
        }
        executor.close();
    }

    axiom::async::Executor executor{1};
    Runtime runtime;
    ResourceRegistry resources;
    TaskRegistry tasks;
    CommandDispatcher dispatcher{runtime, resources, tasks};
    std::string resource_id;
    std::string task_id;
};

} // namespace

TEST(CommandSchemaContract, ActionDescribeEncodesTheCompleteDescriptor) {
    const SchemaWorld world;
    const auto described =
        world.dispatcher.dispatch(action_describe, object({{"action", Value{"schema.echo"}}}), {});
    ASSERT_TRUE(described);
    expectWholeValue(described.value(), echoActionValue());
}

TEST(CommandSchemaContract, ActionListEncodesCompleteDescriptorsAndFilters) {
    const SchemaWorld world;
    const auto expected = Value{Value::Array{echoActionValue()}};
    expectWholeValue(world.dispatcher.dispatch(action_list, {}, {}).value(), expected);
    expectWholeValue(
        world.dispatcher.dispatch(action_list, object({{"module", Value{"schema"}}}), {}).value(),
        expected);
    expectWholeValue(
        world.dispatcher
            .dispatch(action_list, object({{"tags", Value{Value::Array{Value{"read"}}}}}), {})
            .value(),
        expected);
    expectWholeValue(
        world.dispatcher
            .dispatch(action_list, object({{"tags", Value{Value::Array{Value{"missing"}}}}}), {})
            .value(),
        Value{Value::Array{}});
    expectWholeValue(world.dispatcher
                         .dispatch(action_list,
                                   object({{"module", Value{"missing"}},
                                           {"tags", Value{Value::Array{Value{"core"}}}}}),
                                   {})
                         .value(),
                     Value{Value::Array{}});
}

TEST(CommandSchemaContract, ActionInvokeAppliesDefaultsAndValidatesArguments) {
    const SchemaWorld world;
    const auto defaults =
        world.dispatcher.dispatch(action_invoke,
                                  object({{"action", Value{"schema.echo"}},
                                          {"arguments", Value{object({{"count", Value{3}}})}}}),
                                  {});
    ASSERT_TRUE(defaults);
    expectWholeValue(defaults.value(), Value{std::int64_t{7}});
    const auto complete = world.dispatcher.dispatch(
        action_invoke,
        object({{"action", Value{"schema.echo"}},
                {"arguments", Value{object({{"count", Value{1}}, {"label", Value{"ab"}}})}}}),
        {});
    ASSERT_TRUE(complete);
    expectWholeValue(complete.value(), Value{std::int64_t{3}});
    const auto missing =
        world.dispatcher.dispatch(action_invoke,
                                  object({{"action", Value{"schema.echo"}},
                                          {"arguments", Value{object({{"label", Value{"x"}}})}}}),
                                  {});
    expectWholeError(missing.error(), Error{.code = ErrorCode::MissingArgument,
                                            .message = "Required argument is missing: count",
                                            .path = "count",
                                            .details = std::nullopt});
    const auto unknown = world.dispatcher.dispatch(
        action_invoke,
        object({{"action", Value{"schema.echo"}},
                {"arguments", Value{object({{"count", Value{1}}, {"bonus", Value{true}}})}}}),
        {});
    expectWholeError(unknown.error(), Error{.code = ErrorCode::UnknownArgument,
                                            .message = "Unknown argument: bonus",
                                            .path = "bonus",
                                            .details = std::nullopt});
    const auto mismatch =
        world.dispatcher.dispatch(action_invoke,
                                  object({{"action", Value{"schema.echo"}},
                                          {"arguments", Value{object({{"count", Value{"3"}}})}}}),
                                  {});
    expectWholeError(mismatch.error(),
                     Error{.code = ErrorCode::TypeMismatch,
                           .message = "Expected integer but received string",
                           .path = "count",
                           .details = Value{object(
                               {{"actual", Value{"string"}}, {"expected", Value{"integer"}}})}});
}

TEST(CommandSchemaContract, ResourceListAndDescribeEncodeTheCompleteDescriptor) {
    const SchemaWorld world;
    const auto expected = Value{Value::Array{resourceValue(world.resource_id)}};
    expectWholeValue(world.dispatcher.dispatch(resource_list, {}, {}).value(), expected);
    expectWholeValue(
        world.dispatcher.dispatch(resource_list, object({{"type", Value{"widget"}}}), {}).value(),
        expected);
    expectWholeValue(
        world.dispatcher.dispatch(resource_list, object({{"type", Value{"geometry"}}}), {}).value(),
        Value{Value::Array{}});
    const auto described = world.dispatcher.dispatch(
        resource_describe, object({{"resource", Value{world.resource_id}}}), {});
    ASSERT_TRUE(described);
    expectWholeValue(described.value(), expected.asArray().front());
}

TEST(CommandSchemaContract, TaskListDescribeAndCancelEncodeTheCompleteDescriptor) {
    const SchemaWorld world;
    const auto expected = Value{Value::Array{taskValue(world.task_id)}};
    expectWholeValue(world.dispatcher.dispatch(task_list, {}, {}).value(), expected);
    expectWholeValue(
        world.dispatcher.dispatch(task_list, object({{"state", Value{"completed"}}}), {}).value(),
        expected);
    expectWholeValue(
        world.dispatcher.dispatch(task_list, object({{"origin_action", Value{"schema.echo"}}}), {})
            .value(),
        expected);
    const auto described =
        world.dispatcher.dispatch(task_describe, object({{"task", Value{world.task_id}}}), {});
    ASSERT_TRUE(described);
    expectWholeValue(described.value(), expected.asArray().front());
    const auto cancelled =
        world.dispatcher.dispatch(task_cancel, object({{"task", Value{world.task_id}}}), {});
    ASSERT_TRUE(cancelled);
    expectWholeValue(cancelled.value(), Value{nullptr});
}

TEST(CommandSchemaContract, SnapshotEncodesTheFourCollectionSchema) {
    const SchemaWorld world;
    const auto snapshot = world.dispatcher.dispatch(system_snapshot, {}, {});
    ASSERT_TRUE(snapshot);
    Value expected_module{object({{"description", Value{"Schema module"}},
                                  {"metadata", Value{object({{"owner", Value{"tests"}}})}},
                                  {"namespace", Value{"schema"}},
                                  {"tags", Value{Value::Array{Value{"core"}}}},
                                  {"version", Value{"1.0"}}})};
    Value::Array actions;
    actions.push_back(echoActionValue());
    Value::Array modules;
    modules.push_back(std::move(expected_module));
    Value::Array resources;
    resources.push_back(resourceValue(world.resource_id));
    Value::Array tasks;
    tasks.push_back(taskValue(world.task_id));
    expectWholeValue(snapshot.value(), Value{object({{"actions", Value{std::move(actions)}},
                                                     {"modules", Value{std::move(modules)}},
                                                     {"resources", Value{std::move(resources)}},
                                                     {"tasks", Value{std::move(tasks)}}})});
}

TEST(CommandSchemaContract, SchemaMatrixReportsDeterministicStructuralErrors) {
    const SchemaWorld world;
    expectWholeError(
        world.dispatcher.dispatch(action_describe, object({{"extra", Value{true}}}), {}).error(),
        Error{.code = ErrorCode::MissingArgument,
              .message = "Required argument is missing: action",
              .path = "action",
              .details = std::nullopt});
    expectWholeError(world.dispatcher
                         .dispatch(action_describe,
                                   object({{"action", Value{"schema.echo"}}, {"zzz", Value{1}}}),
                                   {})
                         .error(),
                     Error{.code = ErrorCode::UnknownArgument,
                           .message = "Unknown argument: zzz",
                           .path = "zzz",
                           .details = std::nullopt});
    expectWholeError(
        world.dispatcher.dispatch(action_list, object({{"module", Value{true}}}), {}).error(),
        Error{.code = ErrorCode::TypeMismatch,
              .message = "Expected string but received boolean",
              .path = "module",
              .details =
                  Value{object({{"actual", Value{"boolean"}}, {"expected", Value{"string"}}})}});
    expectWholeError(world.dispatcher.dispatch("schema.nope", {}, {}).error(),
                     Error{.code = ErrorCode::UnknownCommand,
                           .message = "Unknown command: schema.nope",
                           .path = std::nullopt,
                           .details = std::nullopt});
}

TEST(CommandSchemaContract, EnumStringsEncodeTheCompleteStableSet) {
    using axiom::command::errorCodeName;
    using axiom::command::detail::taskStateName;
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
    EXPECT_EQ(errorCodeName(ErrorCode::HostClosed), "host_closed");
    EXPECT_EQ(taskStateName(axiom::task::TaskState::Pending), "pending");
    EXPECT_EQ(taskStateName(axiom::task::TaskState::Running), "running");
    EXPECT_EQ(taskStateName(axiom::task::TaskState::Completed), "completed");
    EXPECT_EQ(taskStateName(axiom::task::TaskState::Failed), "failed");
    EXPECT_EQ(taskStateName(axiom::task::TaskState::Cancelled), "cancelled");
}

TEST(CommandSchemaContract, TypeDescriptorEncodesEveryKindAndNullableShape) {
    using axiom::command::detail::encodeTypeDescriptor;
    constexpr std::array kinds{
        std::pair{TypeDescriptor::Kind::Null, std::string_view{"null"}},
        std::pair{TypeDescriptor::Kind::Boolean, std::string_view{"boolean"}},
        std::pair{TypeDescriptor::Kind::Integer, std::string_view{"integer"}},
        std::pair{TypeDescriptor::Kind::Number, std::string_view{"number"}},
        std::pair{TypeDescriptor::Kind::String, std::string_view{"string"}},
    };
    for(const auto& [kind, kind_name] : kinds) {
        expectWholeValue(encodeTypeDescriptor({.kind = kind,
                                               .nullable = false,
                                               .description = {},
                                               .element_type = {},
                                               .fields = {},
                                               .value_type = {}}),
                         Value{object({{"description", Value{""}},
                                       {"kind", Value{std::string{kind_name}}},
                                       {"nullable", Value{false}}})});
    }
    const auto nullable_integer = TypeDescriptor{
        .kind = TypeDescriptor::Kind::Integer,
        .nullable = true,
        .description = {},
        .element_type = {},
        .fields = {},
        .value_type = {},
    };
    expectWholeValue(
        encodeTypeDescriptor(nullable_integer),
        Value{object(
            {{"description", Value{""}}, {"kind", Value{"integer"}}, {"nullable", Value{true}}})});
    const auto array_type = TypeDescriptor::array(nullable_integer);
    expectWholeValue(encodeTypeDescriptor(array_type),
                     Value{object({{"description", Value{""}},
                                   {"kind", Value{"array"}},
                                   {"nullable", Value{false}},
                                   {"element_type", Value{object({{"description", Value{""}},
                                                                  {"kind", Value{"integer"}},
                                                                  {"nullable", Value{true}}})}}})});
    const auto fields = TypeDescriptor::Fields{{"size", TypeDescriptor::nested(nullable_integer)}};
    expectWholeValue(
        encodeTypeDescriptor(TypeDescriptor::object(fields)),
        Value{object(
            {{"description", Value{""}},
             {"kind", Value{"object"}},
             {"nullable", Value{false}},
             {"fields", Value{object({{"size", Value{object({{"description", Value{""}},
                                                             {"kind", Value{"integer"}},
                                                             {"nullable", Value{true}}})}}})}}})});
    expectWholeValue(encodeTypeDescriptor(TypeDescriptor::objectValues(nullable_integer)),
                     Value{object({{"description", Value{""}},
                                   {"kind", Value{"object"}},
                                   {"nullable", Value{false}},
                                   {"value_type", Value{object({{"description", Value{""}},
                                                                {"kind", Value{"integer"}},
                                                                {"nullable", Value{true}}})}}})});
}

TEST(CommandSchemaContract, ErrorEncodingPreservesAllFourFieldsAndOmissionRules) {
    using axiom::command::detail::encodeError;
    expectWholeValue(
        encodeError(Error{.code = ErrorCode::InternalError,
                          .message = "plain",
                          .path = std::nullopt,
                          .details = std::nullopt}),
        Value{object({{"code", Value{"internal_error"}}, {"message", Value{"plain"}}})});
    expectWholeValue(encodeError(Error{.code = ErrorCode::TypeMismatch,
                                       .message = "bad",
                                       .path = "items[0]",
                                       .details = Value{object({{"expected", Value{"integer"}}})}}),
                     Value{object({{"code", Value{"type_mismatch"}},
                                   {"details", Value{object({{"expected", Value{"integer"}}})}},
                                   {"message", Value{"bad"}},
                                   {"path", Value{"items[0]"}}})});
}

TEST(CommandSchemaContract, OmitsUnspecifiedVersionAndOriginFields) {
    axiom::async::Executor executor{1};
    Runtime runtime;
    ResourceRegistry resources;
    TaskRegistry tasks;
    const CommandDispatcher dispatcher{runtime, resources, tasks};
    ModuleBuilder plain{{.namespace_name = "bare",
                         .description = "Bare module",
                         .version = {},
                         .tags = {},
                         .metadata = {}}};
    EXPECT_TRUE(plain.add("noop", "Noop", [] { return 0; }));
    EXPECT_TRUE(runtime.registerModule(std::move(plain)));
    auto submitted = tasks.submit(executor, "originless", [](axiom::task::TaskContext&) {
        return axiom::Result<void>::success();
    });
    EXPECT_TRUE(submitted);
    executor.close();
    const auto module_page = dispatcher.dispatch(system_snapshot, object({}), {});
    ASSERT_TRUE(module_page);
    const auto& modules = module_page.value().asObject().at("modules").asArray();
    ASSERT_EQ(modules.size(), 1U);
    expectWholeValue(modules.front(), Value{object({{"description", Value{"Bare module"}},
                                                    {"metadata", Value{object({})}},
                                                    {"namespace", Value{"bare"}},
                                                    {"tags", Value{Value::Array{}}}})});
    const auto actions_page = dispatcher.dispatch(action_list, {}, {});
    ASSERT_TRUE(actions_page);
    expectWholeValue(actions_page.value().asArray().front(),
                     Value{object({{"description", Value{"Noop"}},
                                   {"id", Value{"bare.noop"}},
                                   {"metadata", Value{object({})}},
                                   {"parameters", Value{Value::Array{}}},
                                   {"return_type", integerType()},
                                   {"tags", Value{Value::Array{}}}})});
    const auto described = dispatcher.dispatch(
        task_describe, object({{"task", Value{std::string{submitted.value().id().str()}}}}), {});
    ASSERT_TRUE(described);
    expectWholeValue(
        described.value(),
        Value{object({
            {"id", Value{std::string{submitted.value().id().str()}}},
            {"name", Value{"originless"}},
            {"progress", Value{object({{"message", Value{""}}, {"value", Value{1.0}}})}},
            {"state", Value{"completed"}},
        })});
}