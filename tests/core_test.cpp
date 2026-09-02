#include <axiom/axiom.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

TEST(CoreFrameworkName, ReturnsAxiom) { EXPECT_STREQ(axiom::frameworkName(), "Axiom"); }

TEST(CoreFrameworkName, AcceptsExactName) { EXPECT_TRUE(axiom::isFrameworkName("Axiom")); }

TEST(CoreFrameworkName, RejectsDifferentName) { EXPECT_FALSE(axiom::isFrameworkName("Other")); }

TEST(Value, RepresentsEverySupportedLogicalType) {
    const axiom::Value null_value;
    const axiom::Value boolean_value{true};
    const axiom::Value integer_value{std::int64_t{42}};
    const axiom::Value number_value{3.5};
    const axiom::Value string_value{"Axiom"};
    const axiom::Value array_value{axiom::Value::Array{axiom::Value{1}, axiom::Value{2}}};
    const axiom::Value object_value{axiom::Value::Object{{"name", axiom::Value{"Axiom"}}}};

    EXPECT_TRUE(null_value.isNull());
    EXPECT_TRUE(boolean_value.isBoolean());
    EXPECT_EQ(integer_value.asInteger(), 42);
    EXPECT_DOUBLE_EQ(number_value.asNumber(), 3.5);
    EXPECT_EQ(string_value.asString(), "Axiom");
    EXPECT_EQ(array_value.asArray().size(), 2U);
    EXPECT_EQ(object_value.asObject().at("name").asString(), "Axiom");
}

TEST(Value, ProvidesSafeAccessForEveryPayloadType) {
    const axiom::Value null_value{nullptr};
    const axiom::Value boolean_value{true};
    const axiom::Value integer_value{std::int64_t{4}};
    const axiom::Value number_value{4.5};
    const axiom::Value string_value{std::string{"four"}};
    const axiom::Value array_value{axiom::Value::Array{axiom::Value{4}}};
    const axiom::Value object_value{axiom::Value::Object{{"four", axiom::Value{4}}}};

    EXPECT_EQ(null_value.type(), axiom::Value::Type::Null);
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
    const axiom::Value value{axiom::Value::Object{
        {"zeta", axiom::Value{1}}, {"alpha", axiom::Value{2}}, {"middle", axiom::Value{3}}}};

    std::string keys;
    for(const auto& [key, ignored] : value.asObject()) {
        static_cast<void>(ignored);
        keys += key;
        keys += ',';
    }

    EXPECT_EQ(keys, "alpha,middle,zeta,");
}

TEST(Value, RejectsIncompatibleAccessWithoutUndefinedBehavior) {
    const axiom::Value value{"not a scalar"};

    EXPECT_THROW(static_cast<void>(value.asBoolean()), axiom::ValueTypeError);
    EXPECT_THROW(static_cast<void>(value.asInteger()), axiom::ValueTypeError);
    EXPECT_THROW(static_cast<void>(value.asNumber()), axiom::ValueTypeError);
    EXPECT_THROW(static_cast<void>(value.asArray()), axiom::ValueTypeError);
    EXPECT_THROW(static_cast<void>(value.asObject()), axiom::ValueTypeError);
}

TEST(Value, MoveConstructionTransfersArrayAndResetsSourceToNull) {
    axiom::Value source{axiom::Value::Array{axiom::Value{1}}};
    const axiom::Value& source_view = source;

    const axiom::Value moved{std::move(source)};

    EXPECT_TRUE(source_view.isNull());
    EXPECT_THROW(static_cast<void>(source_view.asArray()), axiom::ValueTypeError);
    EXPECT_EQ(moved.asArray().front().asInteger(), 1);
}

TEST(Value, MoveConstructionTransfersObjectAndResetsSourceToNull) {
    axiom::Value source{axiom::Value::Object{{"name", axiom::Value{"Axiom"}}}};
    const axiom::Value& source_view = source;

    const axiom::Value moved{std::move(source)};

    EXPECT_TRUE(source_view.isNull());
    EXPECT_THROW(static_cast<void>(source_view.asObject()), axiom::ValueTypeError);
    EXPECT_EQ(moved.asObject().at("name").asString(), "Axiom");
}

TEST(Value, MoveAssignmentTransfersArrayAndResetsSourceToNull) {
    axiom::Value source{axiom::Value::Array{axiom::Value{1}}};
    axiom::Value destination{nullptr};
    const axiom::Value& source_view = source;

    destination = std::move(source);

    EXPECT_TRUE(source_view.isNull());
    EXPECT_THROW(static_cast<void>(source_view.asArray()), axiom::ValueTypeError);
    EXPECT_EQ(destination.asArray().front().asInteger(), 1);
}

TEST(Value, MoveAssignmentTransfersObjectAndResetsSourceToNull) {
    axiom::Value source{axiom::Value::Object{{"name", axiom::Value{"Axiom"}}}};
    axiom::Value destination{nullptr};
    const axiom::Value& source_view = source;

    destination = std::move(source);

    EXPECT_TRUE(source_view.isNull());
    EXPECT_THROW(static_cast<void>(source_view.asObject()), axiom::ValueTypeError);
    EXPECT_EQ(destination.asObject().at("name").asString(), "Axiom");
}

TEST(Value, SelfMoveAssignmentPreservesValue) {
    axiom::Value value{axiom::Value::Object{{"answer", axiom::Value{42}}}};

    auto& same_value = value;
    value = std::move(same_value);

    EXPECT_EQ(value.asObject().at("answer").asInteger(), 42);
}

TEST(Value, CopyPreservesArrayAndObjectPayloads) {
    const axiom::Value array{axiom::Value::Array{axiom::Value{42}}};
    const axiom::Value object{axiom::Value::Object{{"answer", axiom::Value{42}}}};

    axiom::Value array_copy = array;
    axiom::Value object_copy = object;

    EXPECT_EQ(array_copy.asArray().front().asInteger(), 42);
    EXPECT_EQ(object_copy.asObject().at("answer").asInteger(), 42);

    array_copy = axiom::Value{nullptr};
    object_copy = axiom::Value{nullptr};

    EXPECT_EQ(array.asArray().front().asInteger(), 42);
    EXPECT_EQ(object.asObject().at("answer").asInteger(), 42);
}

TEST(Arguments, IsAnOrderedNamedValueObject) {
    const axiom::Arguments arguments{{"second", axiom::Value{2}}, {"first", axiom::Value{1}}};

    EXPECT_EQ(arguments.at("first").asInteger(), 1);
    EXPECT_EQ(arguments.begin()->first, "first");
}

TEST(Error, AppendsCancellationWithoutRenumberingExistingCodes) {
    EXPECT_EQ(static_cast<int>(axiom::ErrorCode::InvalidArgument), 0);
    EXPECT_EQ(static_cast<int>(axiom::ErrorCode::MissingArgument), 1);
    EXPECT_EQ(static_cast<int>(axiom::ErrorCode::UnknownArgument), 2);
    EXPECT_EQ(static_cast<int>(axiom::ErrorCode::TypeMismatch), 3);
    EXPECT_EQ(static_cast<int>(axiom::ErrorCode::NotFound), 4);
    EXPECT_EQ(static_cast<int>(axiom::ErrorCode::AlreadyExists), 5);
    EXPECT_EQ(static_cast<int>(axiom::ErrorCode::InvalidDescriptor), 6);
    EXPECT_EQ(static_cast<int>(axiom::ErrorCode::InvocationFailed), 7);
    EXPECT_EQ(static_cast<int>(axiom::ErrorCode::InternalError), 8);
    EXPECT_EQ(static_cast<int>(axiom::ErrorCode::Cancelled), 9);
    EXPECT_EQ(static_cast<int>(axiom::ErrorCode::UnknownCommand), 10);
}

TEST(Error, RetainsCodeMessagePathAndStructuredDetails) {
    const axiom::Error error{
        .code = axiom::ErrorCode::TypeMismatch,
        .message = "expected integer",
        .path = "points[2]",
        .details = axiom::Value{axiom::Value::Object{{"expected", axiom::Value{"integer"}}}},
    };

    EXPECT_EQ(error.code, axiom::ErrorCode::TypeMismatch);
    EXPECT_EQ(error.message, "expected integer");
    ASSERT_TRUE(error.path.has_value());
    EXPECT_EQ(*error.path, "points[2]");
    ASSERT_TRUE(error.details.has_value());
    EXPECT_EQ(error.details->asObject().at("expected").asString(), "integer");
}

TEST(ResultValue, ExposesSuccessfulValue) {
    const auto result = axiom::Result<axiom::Value>::success(axiom::Value{std::int64_t{7}});

    EXPECT_TRUE(result.hasValue());
    EXPECT_FALSE(result.hasError());
    EXPECT_TRUE(result);
    EXPECT_EQ(result.value().asInteger(), 7);
    EXPECT_THROW(static_cast<void>(result.error()), std::logic_error);
}

TEST(ResultValue, ExposesStructuredFailure) {
    const auto result = axiom::Result<axiom::Value>::failure({
        .code = axiom::ErrorCode::MissingArgument,
        .message = "size",
        .path = "size",
        .details = std::nullopt,
    });

    EXPECT_FALSE(result.hasValue());
    EXPECT_TRUE(result.hasError());
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code, axiom::ErrorCode::MissingArgument);
    EXPECT_THROW(static_cast<void>(result.value()), std::logic_error);
}

TEST(ResultValue, AllowsMutableAccessToTheActiveAlternative) {
    auto successful = axiom::Result<axiom::Value>::success(axiom::Value{1});
    auto failed = axiom::Result<axiom::Value>::failure({
        .code = axiom::ErrorCode::InvalidArgument,
        .message = "invalid",
        .path = std::nullopt,
        .details = std::nullopt,
    });

    successful.value() = axiom::Value{2};
    failed.error().message = "updated";

    EXPECT_EQ(successful.value().asInteger(), 2);
    EXPECT_EQ(failed.error().message, "updated");
}

TEST(ResultVoid, DistinguishesSuccessfulAndFailedOperations) {
    const auto successful = axiom::Result<void>::success();
    const auto failed = axiom::Result<void>::failure({
        .code = axiom::ErrorCode::NotFound,
        .message = "missing",
        .path = std::nullopt,
        .details = std::nullopt,
    });

    EXPECT_TRUE(successful);
    EXPECT_NO_THROW(successful.value());
    EXPECT_FALSE(failed);
    EXPECT_EQ(failed.error().code, axiom::ErrorCode::NotFound);
    EXPECT_THROW(failed.value(), std::logic_error);
    EXPECT_THROW(static_cast<void>(successful.error()), std::logic_error);
}

TEST(ResultVoid, AllowsMutableErrorAccess) {
    auto result = axiom::Result<void>::failure({
        .code = axiom::ErrorCode::InternalError,
        .message = "before",
        .path = std::nullopt,
        .details = std::nullopt,
    });

    result.error().message = "after";

    EXPECT_EQ(result.error().message, "after");
}
TEST(ResultVoid, MutableFailureAccessPreservesStructuredError) {
    auto failure = axiom::Result<void>::failure({
        .code = axiom::ErrorCode::NotFound,
        .message = "missing",
        .path = "resource",
        .details = axiom::Value{7},
    });
    auto& error = failure.error();
    EXPECT_EQ(error.code, axiom::ErrorCode::NotFound);
    EXPECT_EQ(error.path, "resource");
    ASSERT_TRUE(error.details.has_value());
    EXPECT_EQ(error.details.value_or(axiom::Value{}).asInteger(), 7);
    error.message = "changed";
    EXPECT_EQ(std::as_const(failure).error().message, "changed");
}

TEST(ResultValue, RejectsMutableErrorAccessOnSuccess) {
    auto result = axiom::Result<axiom::Value>::success(axiom::Value{7});
    EXPECT_THROW(static_cast<void>(result.error()), std::logic_error);
}

TEST(ResultVoid, RejectsMutableErrorAccessOnSuccess) {
    auto result = axiom::Result<void>::success();
    EXPECT_THROW(static_cast<void>(result.error()), std::logic_error);
}
