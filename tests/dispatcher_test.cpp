#include "../src/action/detail/action.hpp"
#include "../src/action/detail/dispatcher.hpp"
#include "../src/action/detail/registry.hpp"
#include <axiom/action/action_id.hpp>
#include <axiom/action/descriptor.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using axiom::ActionDescriptor;
using axiom::ActionId;
using axiom::Arguments;
using axiom::ErrorCode;
using axiom::InvocationContext;
using axiom::ParameterDescriptor;
using axiom::Result;
using axiom::TypeDescriptor;
using axiom::Value;
using axiom::detail::Dispatcher;
using axiom::detail::IAction;
using axiom::detail::Registry;

class RecordingAction final : public IAction {
public:
    [[nodiscard]] Result<Value> invoke(const Arguments& arguments,
                                       const InvocationContext& context) override {
        received_arguments = &arguments;
        received_context = &context;
        return Result<Value>::success(Value{
            std::int64_t{arguments.at("left").asInteger() + arguments.at("right").asInteger()}});
    }

    const Arguments* received_arguments{nullptr};
    const InvocationContext* received_context{nullptr};
};

class BusinessFailureAction final : public IAction {
public:
    [[nodiscard]] Result<Value> invoke(const Arguments& arguments,
                                       const InvocationContext& context) override {
        static_cast<void>(arguments);
        static_cast<void>(context);
        return Result<Value>::failure({
            .code = ErrorCode::TypeMismatch,
            .message = "business validation failed",
            .path = "shape.size",
            .details = std::nullopt,
        });
    }
};

class StandardExceptionAction final : public IAction {
public:
    [[nodiscard]] Result<Value> invoke(const Arguments& arguments,
                                       const InvocationContext& context) override {
        static_cast<void>(arguments);
        static_cast<void>(context);
        throw std::runtime_error{"unexpected business exception"};
    }
};

class UnknownExceptionAction final : public IAction {
public:
    [[nodiscard]] Result<Value> invoke(const Arguments& arguments,
                                       const InvocationContext& context) override {
        static_cast<void>(arguments);
        static_cast<void>(context);
        throw 7;
    }
};

class ValueAccessExceptionAction final : public IAction {
public:
    [[nodiscard]] Result<Value> invoke(const Arguments& arguments,
                                       const InvocationContext& context) override {
        static_cast<void>(arguments);
        static_cast<void>(context);
        return Result<Value>::success(Value{Value{true}.asInteger()});
    }
};

ActionId actionId(const std::string_view text) {
    auto result = ActionId::parse(text);
    EXPECT_TRUE(result);
    return std::move(result.value());
}

TypeDescriptor integerType() {
    return {.kind = TypeDescriptor::Kind::Integer,
            .description = {},
            .element_type = {},
            .fields = {},
            .value_type = {}};
}

ActionDescriptor action(const std::string_view id,
                        std::vector<ParameterDescriptor> parameters = {}) {
    return {
        .id = actionId(id),
        .description = "Test Action",
        .parameters = std::move(parameters),
        .return_type = integerType(),
        .version = std::nullopt,
        .tags = {},
    };
}

void registerMath(Registry& registry) {
    ASSERT_TRUE(registry.registerModule({.namespace_name = "math", .metadata = {}}));
}

void expectUnknownArgument(const Result<Value>& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::UnknownArgument);
}

void expectEncodedArgumentPath(const Result<Value>& result, const std::string& expected_path) {
    const auto& path = result.error().path;
    ASSERT_TRUE(path.has_value());
    if(!path.has_value()) {
        return;
    }
    EXPECT_EQ(path.value(), expected_path);
}

} // namespace

TEST(Dispatcher, InvokesRegisteredActionAndForwardsDiagnosticContext) {
    Registry registry;
    registerMath(registry);
    auto implementation = std::make_unique<RecordingAction>();
    const RecordingAction* const implementation_view = implementation.get();
    ASSERT_TRUE(registry.registerAction(
        action("math.add",
               {{.name = "left", .description = {}, .type = integerType(), .default_value = {}},
                {.name = "right", .description = {}, .type = integerType(), .default_value = {}}}),
        std::move(implementation)));
    Dispatcher dispatcher{registry};
    const Arguments arguments{{"left", Value{std::int64_t{2}}}, {"right", Value{std::int64_t{3}}}};
    const InvocationContext context{.request_id = "request-7",
                                    .trace_id = "trace-8",
                                    .caller = "adapter",
                                    .metadata = {{"source", "test"}}};

    const auto result = dispatcher.invoke(actionId("math.add"), arguments, context);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().asInteger(), 5);
    EXPECT_EQ(implementation_view->received_arguments, &arguments);
    EXPECT_EQ(implementation_view->received_context, &context);
}

TEST(Dispatcher, ReturnsNotFoundBeforeArgumentValidationForUnknownAction) {
    Registry registry;
    Dispatcher dispatcher{registry};

    const auto result = dispatcher.invoke(actionId("math.add"), {{"unexpected", Value{1}}}, {});

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST(Dispatcher, ReportsFirstMissingRequiredArgumentInDescriptorOrder) {
    Registry registry;
    registerMath(registry);
    ASSERT_TRUE(registry.registerAction(
        action("math.add",
               {{.name = "second", .description = {}, .type = integerType(), .default_value = {}},
                {.name = "first", .description = {}, .type = integerType(), .default_value = {}}}),
        std::make_unique<BusinessFailureAction>()));
    Dispatcher dispatcher{registry};

    const auto result = dispatcher.invoke(actionId("math.add"), {}, {});

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::MissingArgument);
    const auto& path = result.error().path;
    ASSERT_TRUE(path.has_value());
    if(!path.has_value()) {
        return;
    }
    EXPECT_EQ(path.value(), "second");
}

TEST(Dispatcher, RejectsUnknownArgumentAfterRequiredArgumentsArePresent) {
    Registry registry;
    registerMath(registry);
    ASSERT_TRUE(registry.registerAction(
        action("math.add",
               {{.name = "left", .description = {}, .type = integerType(), .default_value = {}},
                {.name = "right", .description = {}, .type = integerType(), .default_value = {}}}),
        std::make_unique<BusinessFailureAction>()));
    Dispatcher dispatcher{registry};

    const auto result = dispatcher.invoke(
        actionId("math.add"), {{"left", Value{1}}, {"right", Value{2}}, {"extra", Value{3}}}, {});

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::UnknownArgument);
    const auto& path = result.error().path;
    ASSERT_TRUE(path.has_value());
    if(!path.has_value()) {
        return;
    }
    EXPECT_EQ(path.value(), "extra");
}

TEST(Dispatcher, EncodesSpecialUnknownArgumentKeysWithTheObjectPathGrammar) {
    Registry registry;
    registerMath(registry);
    ASSERT_TRUE(registry.registerAction(
        action("math.add",
               {{.name = "left", .description = {}, .type = integerType(), .default_value = {}},
                {.name = "right", .description = {}, .type = integerType(), .default_value = {}}}),
        std::make_unique<BusinessFailureAction>()));
    Dispatcher dispatcher{registry};
    const Arguments base{{"left", Value{1}}, {"right", Value{2}}};

    auto dotted = base;
    dotted.emplace("a.b", Value{3});
    auto bracketed = base;
    bracketed.emplace("items[0]", Value{3});
    auto empty = base;
    empty.emplace("", Value{3});

    const auto dotted_result = dispatcher.invoke(actionId("math.add"), dotted, {});
    const auto bracketed_result = dispatcher.invoke(actionId("math.add"), bracketed, {});
    const auto empty_result = dispatcher.invoke(actionId("math.add"), empty, {});

    expectUnknownArgument(dotted_result);
    expectEncodedArgumentPath(dotted_result, "[\"a.b\"]");
    expectUnknownArgument(bracketed_result);
    expectEncodedArgumentPath(bracketed_result, "[\"items[0]\"]");
    expectUnknownArgument(empty_result);
    expectEncodedArgumentPath(empty_result, "[\"\"]");
}

TEST(Dispatcher, PreservesBusinessErrorCodeAndPath) {
    Registry registry;
    registerMath(registry);
    ASSERT_TRUE(
        registry.registerAction(action("math.add"), std::make_unique<BusinessFailureAction>()));
    Dispatcher dispatcher{registry};

    const auto result = dispatcher.invoke(actionId("math.add"), {}, {});

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::TypeMismatch);
    const auto& path = result.error().path;
    ASSERT_TRUE(path.has_value());
    if(!path.has_value()) {
        return;
    }
    EXPECT_EQ(path.value(), "shape.size");
}

TEST(Dispatcher, NormalizesStandardExceptionsAtTheInvocationBoundary) {
    Registry registry;
    registerMath(registry);
    ASSERT_TRUE(
        registry.registerAction(action("math.add"), std::make_unique<StandardExceptionAction>()));
    Dispatcher dispatcher{registry};

    const auto result = dispatcher.invoke(actionId("math.add"), {}, {});

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::InvocationFailed);
}

TEST(Dispatcher, NormalizesValueAccessExceptionsAtTheInvocationBoundary) {
    Registry registry;
    registerMath(registry);
    ASSERT_TRUE(registry.registerAction(action("math.add"),
                                        std::make_unique<ValueAccessExceptionAction>()));
    Dispatcher dispatcher{registry};

    const auto result = dispatcher.invoke(actionId("math.add"), {}, {});

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::InvocationFailed);
}

TEST(Dispatcher, NormalizesUnknownExceptionsAtTheInvocationBoundary) {
    Registry registry;
    registerMath(registry);
    ASSERT_TRUE(
        registry.registerAction(action("math.add"), std::make_unique<UnknownExceptionAction>()));
    Dispatcher dispatcher{registry};

    const auto result = dispatcher.invoke(actionId("math.add"), {}, {});

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::InternalError);
}
