#include <axiom/core/base/error.hpp>
#include <axiom/core/base/result.hpp>
#include <axiom/core/base/value.hpp>
#include <axiom/core/core.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

TEST(CoreFrameworkName, ReturnsAxiom) { EXPECT_STREQ(axiom::core::frameworkName(), "Axiom"); }

TEST(CoreFrameworkName, AcceptsExactName) { EXPECT_TRUE(axiom::core::isFrameworkName("Axiom")); }

TEST(CoreFrameworkName, RejectsDifferentName) {
    EXPECT_FALSE(axiom::core::isFrameworkName("Other"));
}

TEST(Value, RepresentsEverySupportedLogicalType) {
    const axiom::core::Value null_value;
    const axiom::core::Value boolean_value{true};
    const axiom::core::Value integer_value{std::int64_t{42}};
    const axiom::core::Value number_value{3.5};
    const axiom::core::Value string_value{"Axiom"};
    const axiom::core::Value array_value{
        axiom::core::Value::Array{axiom::core::Value{1}, axiom::core::Value{2}}};
    const axiom::core::Value object_value{
        axiom::core::Value::Object{{"name", axiom::core::Value{"Axiom"}}}};

    EXPECT_TRUE(null_value.isNull());
    EXPECT_TRUE(boolean_value.isBoolean());
    EXPECT_EQ(integer_value.asInteger(), 42);
    EXPECT_DOUBLE_EQ(number_value.asNumber(), 3.5);
    EXPECT_EQ(string_value.asString(), "Axiom");
    EXPECT_EQ(array_value.asArray().size(), 2U);
    EXPECT_EQ(object_value.asObject().at("name").asString(), "Axiom");
}

TEST(Value, ProvidesSafeAccessForEveryPayloadType) {
    const axiom::core::Value null_value{nullptr};
    const axiom::core::Value boolean_value{true};
    const axiom::core::Value integer_value{std::int64_t{4}};
    const axiom::core::Value number_value{4.5};
    const axiom::core::Value string_value{std::string{"four"}};
    const axiom::core::Value array_value{axiom::core::Value::Array{axiom::core::Value{4}}};
    const axiom::core::Value object_value{
        axiom::core::Value::Object{{"four", axiom::core::Value{4}}}};

    EXPECT_EQ(null_value.type(), axiom::core::Value::Type::Null);
    EXPECT_TRUE(null_value.isNull());
    EXPECT_TRUE(boolean_value.isBoolean());
    EXPECT_TRUE(integer_value.isInteger());
    EXPECT_TRUE(number_value.isNumber());
    EXPECT_TRUE(string_value.isString());
    EXPECT_TRUE(array_value.isArray());
    EXPECT_TRUE(object_value.isObject());
    EXPECT_TRUE(boolean_value.asBoolean());
    EXPECT_EQ(integer_value.asInteger(), 4);
    EXPECT_DOUBLE_EQ(number_value.asNumber(), 4.5);
    EXPECT_EQ(string_value.asString(), "four");
    EXPECT_EQ(array_value.asArray().front().asInteger(), 4);
    EXPECT_EQ(object_value.asObject().at("four").asInteger(), 4);
}

TEST(Value, IteratesObjectKeysInStableLexicographicOrder) {
    const axiom::core::Value value{axiom::core::Value::Object{{"zeta", axiom::core::Value{1}},
                                                              {"alpha", axiom::core::Value{2}},
                                                              {"middle", axiom::core::Value{3}}}};

    std::string keys;
    for(const auto& [key, ignored] : value.asObject()) {
        static_cast<void>(ignored);
        keys += key;
        keys += ',';
    }

    EXPECT_EQ(keys, "alpha,middle,zeta,");
}

TEST(Value, RejectsIncompatibleAccessWithoutUndefinedBehavior) {
    const axiom::core::Value value{"not a scalar"};

    EXPECT_THROW(static_cast<void>(value.asBoolean()), axiom::core::ValueTypeError);
    EXPECT_THROW(static_cast<void>(value.asInteger()), axiom::core::ValueTypeError);
    EXPECT_THROW(static_cast<void>(value.asNumber()), axiom::core::ValueTypeError);
    EXPECT_THROW(static_cast<void>(value.asArray()), axiom::core::ValueTypeError);
    EXPECT_THROW(static_cast<void>(value.asObject()), axiom::core::ValueTypeError);
}

TEST(Value, MoveConstructionTransfersArrayAndResetsSourceToNull) {
    axiom::core::Value source{axiom::core::Value::Array{axiom::core::Value{1}}};
    const axiom::core::Value& source_view = source;

    const axiom::core::Value moved{std::move(source)};

    EXPECT_TRUE(source_view.isNull());
    EXPECT_THROW(static_cast<void>(source_view.asArray()), axiom::core::ValueTypeError);
    EXPECT_EQ(moved.asArray().front().asInteger(), 1);
}

TEST(Value, MoveConstructionTransfersObjectAndResetsSourceToNull) {
    axiom::core::Value source{axiom::core::Value::Object{{"name", axiom::core::Value{"Axiom"}}}};
    const axiom::core::Value& source_view = source;

    const axiom::core::Value moved{std::move(source)};

    EXPECT_TRUE(source_view.isNull());
    EXPECT_THROW(static_cast<void>(source_view.asObject()), axiom::core::ValueTypeError);
    EXPECT_EQ(moved.asObject().at("name").asString(), "Axiom");
}

TEST(Value, MoveAssignmentTransfersArrayAndResetsSourceToNull) {
    axiom::core::Value source{axiom::core::Value::Array{axiom::core::Value{1}}};
    axiom::core::Value destination{nullptr};
    const axiom::core::Value& source_view = source;

    destination = std::move(source);

    EXPECT_TRUE(source_view.isNull());
    EXPECT_THROW(static_cast<void>(source_view.asArray()), axiom::core::ValueTypeError);
    EXPECT_EQ(destination.asArray().front().asInteger(), 1);
}

TEST(Value, MoveAssignmentTransfersObjectAndResetsSourceToNull) {
    axiom::core::Value source{axiom::core::Value::Object{{"name", axiom::core::Value{"Axiom"}}}};
    axiom::core::Value destination{nullptr};
    const axiom::core::Value& source_view = source;

    destination = std::move(source);

    EXPECT_TRUE(source_view.isNull());
    EXPECT_THROW(static_cast<void>(source_view.asObject()), axiom::core::ValueTypeError);
    EXPECT_EQ(destination.asObject().at("name").asString(), "Axiom");
}

TEST(Value, SelfMoveAssignmentPreservesValue) {
    axiom::core::Value value{axiom::core::Value::Object{{"answer", axiom::core::Value{42}}}};

    auto& same_value = value;
    value = std::move(same_value);

    EXPECT_EQ(value.asObject().at("answer").asInteger(), 42);
}

TEST(Value, CopyPreservesArrayAndObjectPayloads) {
    const axiom::core::Value array{axiom::core::Value::Array{axiom::core::Value{42}}};
    const axiom::core::Value object{axiom::core::Value::Object{{"answer", axiom::core::Value{42}}}};

    axiom::core::Value array_copy = array;
    axiom::core::Value object_copy = object;

    EXPECT_EQ(array_copy.asArray().front().asInteger(), 42);
    EXPECT_EQ(object_copy.asObject().at("answer").asInteger(), 42);

    array_copy = axiom::core::Value{nullptr};
    object_copy = axiom::core::Value{nullptr};

    EXPECT_EQ(array.asArray().front().asInteger(), 42);
    EXPECT_EQ(object.asObject().at("answer").asInteger(), 42);
}

TEST(Arguments, IsAnOrderedNamedValueObject) {
    const axiom::core::Arguments arguments{{"second", axiom::core::Value{2}},
                                           {"first", axiom::core::Value{1}}};

    EXPECT_EQ(arguments.at("first").asInteger(), 1);
    EXPECT_EQ(arguments.begin()->first, "first");
}

TEST(Error, RetainsCodeMessagePathAndStructuredDetails) {
    const axiom::core::Error error{
        .code = axiom::core::ErrorCode::TypeMismatch,
        .message = "expected integer",
        .path = "points[2]",
        .details = axiom::core::Value{axiom::core::Value::Object{
            {"expected", axiom::core::Value{"integer"}}}},
    };

    EXPECT_EQ(error.code, axiom::core::ErrorCode::TypeMismatch);
    EXPECT_EQ(error.message, "expected integer");
    ASSERT_TRUE(error.path.has_value());
    EXPECT_EQ(*error.path, "points[2]");
    ASSERT_TRUE(error.details.has_value());
    EXPECT_EQ(error.details->asObject().at("expected").asString(), "integer");
}

TEST(ResultValue, ExposesSuccessfulValue) {
    const auto result =
        axiom::core::Result<axiom::core::Value>::success(axiom::core::Value{std::int64_t{7}});

    EXPECT_TRUE(result.hasValue());
    EXPECT_FALSE(result.hasError());
    EXPECT_TRUE(result);
    EXPECT_EQ(result.value().asInteger(), 7);
    EXPECT_THROW(static_cast<void>(result.error()), std::logic_error);
}

TEST(ResultValue, ExposesStructuredFailure) {
    const auto result = axiom::core::Result<axiom::core::Value>::failure({
        .code = axiom::core::ErrorCode::MissingArgument,
        .message = "size",
        .path = "size",
        .details = std::nullopt,
    });

    EXPECT_FALSE(result.hasValue());
    EXPECT_TRUE(result.hasError());
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code, axiom::core::ErrorCode::MissingArgument);
    EXPECT_THROW(static_cast<void>(result.value()), std::logic_error);
}

TEST(ResultValue, AllowsMutableAccessToTheActiveAlternative) {
    auto successful = axiom::core::Result<axiom::core::Value>::success(axiom::core::Value{1});
    auto failed = axiom::core::Result<axiom::core::Value>::failure({
        .code = axiom::core::ErrorCode::InvalidArgument,
        .message = "invalid",
        .path = std::nullopt,
        .details = std::nullopt,
    });

    successful.value() = axiom::core::Value{2};
    failed.error().message = "updated";

    EXPECT_EQ(successful.value().asInteger(), 2);
    EXPECT_EQ(failed.error().message, "updated");
}

TEST(ResultVoid, DistinguishesSuccessfulAndFailedOperations) {
    const auto successful = axiom::core::Result<void>::success();
    const auto failed = axiom::core::Result<void>::failure({
        .code = axiom::core::ErrorCode::NotFound,
        .message = "missing",
        .path = std::nullopt,
        .details = std::nullopt,
    });

    EXPECT_TRUE(successful);
    EXPECT_NO_THROW(successful.value());
    EXPECT_FALSE(failed);
    EXPECT_EQ(failed.error().code, axiom::core::ErrorCode::NotFound);
    EXPECT_THROW(failed.value(), std::logic_error);
    EXPECT_THROW(static_cast<void>(successful.error()), std::logic_error);
}

TEST(ResultVoid, AllowsMutableErrorAccess) {
    auto result = axiom::core::Result<void>::failure({
        .code = axiom::core::ErrorCode::InternalError,
        .message = "before",
        .path = std::nullopt,
        .details = std::nullopt,
    });

    result.error().message = "after";

    EXPECT_EQ(result.error().message, "after");
}
TEST(ResultVoid, MutableFailureAccessPreservesStructuredError) {
    auto failure = axiom::core::Result<void>::failure({
        .code = axiom::core::ErrorCode::NotFound,
        .message = "missing",
        .path = "resource",
        .details = axiom::core::Value{7},
    });
    auto& error = failure.error();
    EXPECT_EQ(error.code, axiom::core::ErrorCode::NotFound);
    EXPECT_EQ(error.path, "resource");
    ASSERT_TRUE(error.details.has_value());
    EXPECT_EQ(error.details.value_or(axiom::core::Value{}).asInteger(), 7);
    error.message = "changed";
    EXPECT_EQ(std::as_const(failure).error().message, "changed");
}

TEST(ResultValue, RejectsMutableErrorAccessOnSuccess) {
    auto result = axiom::core::Result<axiom::core::Value>::success(axiom::core::Value{7});
    EXPECT_THROW(static_cast<void>(result.error()), std::logic_error);
}

TEST(ResultVoid, RejectsMutableErrorAccessOnSuccess) {
    auto result = axiom::core::Result<void>::success();
    EXPECT_THROW(static_cast<void>(result.error()), std::logic_error);
}
