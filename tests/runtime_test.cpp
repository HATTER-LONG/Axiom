#include <axiom/runtime/action.hpp>
#include <axiom/runtime/action_id.hpp>
#include <axiom/runtime/descriptor.hpp>
#include <axiom/runtime/detail/action_adapter.hpp>
#include <axiom/runtime/detail/function_traits.hpp>
#include <axiom/runtime/detail/registry.hpp>
#include <axiom/runtime/detail/value_converter.hpp>
#include <axiom/runtime/invocation_context.hpp>
#include <axiom/runtime/module.hpp>
#include <axiom/runtime/result.hpp>
#include <axiom/runtime/runtime.hpp>
#include <axiom/runtime/value.hpp>

#include "dispatcher.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

using axiom::runtime::ActionDescriptor;
using axiom::runtime::ActionId;
using axiom::runtime::Arguments;
using axiom::runtime::Error;
using axiom::runtime::ErrorCode;
using axiom::runtime::IAction;
using axiom::runtime::InvocationContext;
using axiom::runtime::ModuleBuilder;
using axiom::runtime::ModuleDescriptor;
using axiom::runtime::optionalParam;
using axiom::runtime::param;
using axiom::runtime::ParameterDescriptor;
using axiom::runtime::Result;
using axiom::runtime::Runtime;
using axiom::runtime::Value;
using axiom::runtime::ValueType;
using axiom::runtime::detail::Dispatcher;
using axiom::runtime::detail::FunctionTraits;
using axiom::runtime::detail::makeTypedAction;
using axiom::runtime::detail::Registry;
using axiom::runtime::detail::TypedActionAdapter;
using axiom::runtime::detail::ValueConverter;

static_assert(axiom::runtime::ValueTypeOf<bool>::VALUE == ValueType::Boolean);
static_assert(axiom::runtime::ValueTypeOf<int>::VALUE == ValueType::Integer);
static_assert(axiom::runtime::ValueTypeOf<std::int64_t>::VALUE == ValueType::Integer);
static_assert(axiom::runtime::ValueTypeOf<float>::VALUE == ValueType::Number);
static_assert(axiom::runtime::ValueTypeOf<double>::VALUE == ValueType::Number);
static_assert(axiom::runtime::ValueTypeOf<std::string>::VALUE == ValueType::String);
static_assert(axiom::runtime::ValueTypeOf<const char*>::VALUE == ValueType::String);
static_assert(axiom::runtime::ValueTypeOf<std::vector<int>>::VALUE == ValueType::Array);
static_assert(axiom::runtime::ValueTypeOf<std::unordered_map<std::string, double>>::VALUE ==
              ValueType::Object);
static_assert(axiom::runtime::ValueTypeOf<void>::VALUE == ValueType::Null);

TEST(RuntimeValue, ConstructFromNullReportsNullType) {
    const Value default_constructed{};
    EXPECT_EQ(default_constructed.type(), ValueType::Null);
    const Value from_nullptr{nullptr};
    EXPECT_EQ(from_nullptr.type(), ValueType::Null);
    EXPECT_TRUE(default_constructed.isNull());
}

TEST(RuntimeValue, ConstructFromBooleanReportsBooleanType) {
    const Value value{true};
    EXPECT_EQ(value.type(), ValueType::Boolean);
    EXPECT_TRUE(value.isBoolean());
}

TEST(RuntimeValue, ConstructFromInt64ReportsIntegerType) {
    const Value value{std::int64_t{-42}};
    EXPECT_EQ(value.type(), ValueType::Integer);
    EXPECT_TRUE(value.isInteger());
}

TEST(RuntimeValue, ConstructFromIntReportsIntegerType) {
    const Value value{7};
    EXPECT_EQ(value.type(), ValueType::Integer);
    EXPECT_TRUE(value.isInteger());
}

TEST(RuntimeValue, ConstructFromDoubleReportsNumberType) {
    const Value value{2.5};
    EXPECT_EQ(value.type(), ValueType::Number);
    EXPECT_FALSE(value.isInteger());
    EXPECT_TRUE(value.isNumber());
}

TEST(RuntimeValue, ConstructFromStringReportsStringType) {
    const Value value{std::string{"axiom"}};
    EXPECT_EQ(value.type(), ValueType::String);
    EXPECT_TRUE(value.isString());
}

TEST(RuntimeValue, ConstructFromCStringReportsStringType) {
    const Value value{"axiom"};
    EXPECT_EQ(value.type(), ValueType::String);
    EXPECT_TRUE(value.isString());
}

TEST(RuntimeValue, ConstructFromArrayReportsArrayType) {
    const Value::Array elements{Value{1}, Value{"two"}};
    const Value value{elements};
    EXPECT_EQ(value.type(), ValueType::Array);
    EXPECT_TRUE(value.isArray());
}

TEST(RuntimeValue, ConstructFromObjectReportsObjectType) {
    const Value::Object members{{"name", Value{"axiom"}}, {"count", Value{3}}};
    const Value value{members};
    EXPECT_EQ(value.type(), ValueType::Object);
    EXPECT_TRUE(value.isObject());
}

TEST(RuntimeValue, ArgumentsIsValueObject) {
    Arguments arguments{{"object_id", Value{7}}, {"angle", Value{0.5}}};
    const Value value{arguments};
    EXPECT_TRUE(value.isObject());
    EXPECT_EQ(value.asObject().at("object_id"), Value{7});
}

TEST(RuntimeValue, TypeQueriesRejectOtherTypes) {
    const Value value{1};
    EXPECT_FALSE(value.isNull());
    EXPECT_FALSE(value.isBoolean());
    EXPECT_TRUE(value.isInteger());
    EXPECT_TRUE(value.isNumber());
    EXPECT_FALSE(value.isString());
    EXPECT_FALSE(value.isArray());
    EXPECT_FALSE(value.isObject());

    const Value text{"text"};
    EXPECT_FALSE(text.isNull());
    EXPECT_FALSE(text.isBoolean());
    EXPECT_FALSE(text.isInteger());
    EXPECT_FALSE(text.isNumber());
    EXPECT_TRUE(text.isString());
    EXPECT_FALSE(text.isArray());
    EXPECT_FALSE(text.isObject());
}

TEST(RuntimeValue, AsBooleanReturnsStoredBoolean) {
    const Value on{true};
    const Value off{false};
    EXPECT_TRUE(on.asBoolean());
    EXPECT_FALSE(off.asBoolean());
}

TEST(RuntimeValue, AsIntegerReturnsStoredInteger) {
    const Value value{std::int64_t{-9}};
    EXPECT_EQ(value.asInteger(), -9);
}

TEST(RuntimeValue, AsNumberReturnsStoredNumber) {
    const Value value{1.25};
    EXPECT_DOUBLE_EQ(value.asNumber(), 1.25);
}

TEST(RuntimeValue, AsNumberAcceptsIntegerStorage) {
    const Value value{5};
    EXPECT_TRUE(value.isInteger());
    EXPECT_DOUBLE_EQ(value.asNumber(), 5.0);
}

TEST(RuntimeValue, AsStringReturnsStoredString) {
    const Value value{"runtime"};
    EXPECT_EQ(value.asString(), "runtime");
}

TEST(RuntimeValue, AsArrayReturnsStoredElements) {
    const Value::Array elements{Value{1}, Value{2.5}};
    const Value value{elements};
    const Value::Array& stored = value.asArray();
    ASSERT_EQ(stored.size(), 2U);
    EXPECT_EQ(stored[0], Value{1});
    EXPECT_EQ(stored[1], Value{2.5});
}

TEST(RuntimeValue, AsObjectReturnsStoredMembers) {
    const Value::Object members{{"name", Value{"axiom"}}};
    const Value value{members};
    const Value::Object& stored = value.asObject();
    ASSERT_EQ(stored.size(), 1U);
    EXPECT_EQ(stored.at("name"), Value{"axiom"});
}

TEST(RuntimeValue, AccessorsThrowOnWrongType) {
    const Value value{1};
    EXPECT_THROW(static_cast<void>(value.asBoolean()), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(value.asString()), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(value.asArray()), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(value.asObject()), std::invalid_argument);

    const Value number{1.5};
    EXPECT_THROW(static_cast<void>(number.asInteger()), std::invalid_argument);
    const Value flag{false};
    EXPECT_THROW(static_cast<void>(flag.asNumber()), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(flag.asInteger()), std::invalid_argument);
    const Value null{};
    EXPECT_THROW(static_cast<void>(null.asNumber()), std::invalid_argument);
}

TEST(RuntimeValue, EqualValuesCompareEqual) {
    EXPECT_EQ(Value{}, Value{nullptr});
    EXPECT_EQ(Value{true}, Value{true});
    EXPECT_EQ(Value{std::int64_t{7}}, Value{7});
    EXPECT_EQ(Value{2.5}, Value{2.5});
    EXPECT_EQ(Value{"axiom"}, Value{std::string{"axiom"}});
}

TEST(RuntimeValue, NestedContainersCompareEqual) {
    const Value::Array array{Value{1}, Value{"two"}};
    const Value::Array array_copy{Value{1}, Value{"two"}};
    EXPECT_EQ(Value{array}, Value{array_copy});

    const Value::Object outer{{"items", Value{array}}, {"flag", Value{false}}};
    const Value::Object outer_copy{{"items", Value{array_copy}}, {"flag", Value{false}}};
    EXPECT_EQ(Value{outer}, Value{outer_copy});
}

TEST(RuntimeValue, DifferingValuesCompareUnequal) {
    EXPECT_NE(Value{true}, Value{false});
    EXPECT_NE(Value{1}, Value{2});
    EXPECT_NE(Value{1.5}, Value{2.5});
    EXPECT_NE(Value{"left"}, Value{"right"});

    const Value::Array array{Value{1}};
    const Value::Array other_array{Value{2}};
    EXPECT_NE(Value{array}, Value{other_array});

    const Value::Object object{{"key", Value{1}}};
    const Value::Object other_object{{"key", Value{2}}};
    EXPECT_NE(Value{object}, Value{other_object});
}

TEST(RuntimeValue, MixedTypeValuesCompareUnequal) {
    EXPECT_NE(Value{}, Value{0});
    EXPECT_NE(Value{true}, Value{std::int64_t{1}});
    EXPECT_NE(Value{std::int64_t{1}}, Value{1.0});
    EXPECT_NE(Value{"1"}, Value{1});
    EXPECT_NE(Value{Value::Array{}}, Value{Value::Object{}});
}

TEST(RuntimeResult, ValuePathReportsValue) {
    const Result<int> result{42};
    EXPECT_TRUE(result.hasValue());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(result.value(), 42);
}

TEST(RuntimeResult, ErrorPathReportsError) {
    const Result<int> result{
        Error{ErrorCode::InvalidArgument, "bad argument", "math.add.a", Value{"detail"}}};
    EXPECT_FALSE(result.hasValue());
    EXPECT_FALSE(static_cast<bool>(result));
    const Error& error = result.error();
    EXPECT_EQ(error.m_code, ErrorCode::InvalidArgument);
    EXPECT_EQ(error.m_message, "bad argument");
    EXPECT_EQ(error.m_path, "math.add.a");
    EXPECT_EQ(error.m_details, Value{"detail"});
}

TEST(RuntimeResult, ErrorDefaultsToInternalError) {
    const Error error{};
    EXPECT_EQ(error.m_code, ErrorCode::InternalError);
    EXPECT_TRUE(error.m_message.empty());
    EXPECT_TRUE(error.m_path.empty());
    EXPECT_TRUE(error.m_details.isNull());
}

TEST(RuntimeResult, AccessingWrongAlternativeThrows) {
    const Result<int> failed{Error{ErrorCode::TypeMismatch, "wrong type", "", Value{}}};
    EXPECT_THROW(static_cast<void>(failed.value()), std::invalid_argument);

    const Result<int> success{1};
    EXPECT_THROW(static_cast<void>(success.error()), std::invalid_argument);
}

TEST(RuntimeResult, MutableValueAccessModifiesStoredValue) {
    Result<int> result{1};
    result.value() = 7;
    EXPECT_EQ(result.value(), 7);
}

TEST(RuntimeResult, MoveValueAccessConsumesStoredValue) {
    Result<std::string> result{std::string{"payload"}};
    const std::string moved = std::move(result).value();
    EXPECT_EQ(moved, "payload");
}

TEST(RuntimeResult, MoveConstructionTransfersValue) {
    Result<std::string> source{std::string{"payload"}};
    const Result<std::string> destination{std::move(source)};
    EXPECT_TRUE(destination.hasValue());
    EXPECT_EQ(destination.value(), "payload");
}

TEST(RuntimeResult, VoidSuccessReportsValue) {
    const Result<void> result{};
    EXPECT_TRUE(result.hasValue());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_THROW(static_cast<void>(result.error()), std::invalid_argument);
}

TEST(RuntimeResult, VoidErrorPathReportsError) {
    const Result<void> result{Error{ErrorCode::InvocationFailed, "boom", "math.add", Value{}}};
    EXPECT_FALSE(result.hasValue());
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.error().m_code, ErrorCode::InvocationFailed);
    EXPECT_EQ(result.error().m_message, "boom");
}

TEST(RuntimeActionId, ConstructFromFullIdParsesParts) {
    const ActionId id{"math.add"};
    EXPECT_TRUE(id.isValid());
    EXPECT_EQ(id.module(), "math");
    EXPECT_EQ(id.action(), "add");
    EXPECT_EQ(id.toString(), "math.add");
}

TEST(RuntimeActionId, ConstructFromPartsParsesParts) {
    const ActionId id{"geometry", "create_box"};
    EXPECT_TRUE(id.isValid());
    EXPECT_EQ(id.module(), "geometry");
    EXPECT_EQ(id.action(), "create_box");
    EXPECT_EQ(id.toString(), "geometry.create_box");
}

TEST(RuntimeActionId, ToStringRoundTrips) {
    const ActionId original{"math", "add"};
    const ActionId parsed{original.toString()};
    EXPECT_EQ(parsed, original);
    EXPECT_TRUE(parsed.isValid());
}

TEST(RuntimeActionId, ValidIdsPassValidation) {
    const ActionId underscore_parts{"_", "_"};
    EXPECT_TRUE(ActionId{"math.add"}.isValid());
    EXPECT_TRUE(ActionId{"m0_x.a9"}.isValid());
    EXPECT_TRUE(underscore_parts.isValid());
}

TEST(RuntimeActionId, MissingOrExtraSeparatorsFailValidation) {
    const ActionId no_dot{"mathadd"};
    EXPECT_FALSE(no_dot.isValid());
    EXPECT_EQ(no_dot.module(), "mathadd");
    EXPECT_TRUE(no_dot.action().empty());

    const ActionId two_dots{"a.b.c"};
    EXPECT_FALSE(two_dots.isValid());
    EXPECT_EQ(two_dots.module(), "a");
    EXPECT_EQ(two_dots.action(), "b.c");
}

TEST(RuntimeActionId, EmptyPartsFailValidation) {
    EXPECT_FALSE(ActionId{".add"}.isValid());
    EXPECT_FALSE(ActionId{"math."}.isValid());
    EXPECT_FALSE(ActionId{""}.isValid());
    EXPECT_FALSE(ActionId{"."}.isValid());
}

TEST(RuntimeActionId, IllegalCharactersFailValidation) {
    EXPECT_FALSE(ActionId{"Math.add"}.isValid());
    EXPECT_FALSE(ActionId{"math.Add"}.isValid());
    EXPECT_FALSE(ActionId{"math-ad.d"}.isValid());
    EXPECT_FALSE(ActionId{"math .add"}.isValid());
}

TEST(RuntimeActionId, TwoPartConstructorValidatesBothParts) {
    const ActionId uppercase_module{"Math", "add"};
    const ActionId uppercase_action{"math", "Add"};
    const ActionId empty_module{"", "add"};
    const ActionId empty_action{"math", ""};
    const ActionId illegal_character{"math", "a-d"};
    const ActionId valid_parts{"math", "add"};
    EXPECT_FALSE(uppercase_module.isValid());
    EXPECT_FALSE(uppercase_action.isValid());
    EXPECT_FALSE(empty_module.isValid());
    EXPECT_FALSE(empty_action.isValid());
    EXPECT_FALSE(illegal_character.isValid());
    EXPECT_TRUE(valid_parts.isValid());
}

TEST(RuntimeActionId, EqualIdsCompareEqual) {
    const ActionId from_full_id{"math.add"};
    const ActionId from_parts{"math", "add"};
    const ActionId other_action{"math.sub"};
    const ActionId other_module{"calc.add"};
    EXPECT_EQ(from_full_id, from_parts);
    EXPECT_NE(from_full_id, other_action);
    EXPECT_NE(from_full_id, other_module);
}

TEST(RuntimeDescriptor, ParameterDescriptorDefaults) {
    const ParameterDescriptor descriptor{};
    EXPECT_TRUE(descriptor.m_name.empty());
    EXPECT_EQ(descriptor.m_type, ValueType::Null);
    EXPECT_TRUE(descriptor.m_description.empty());
    EXPECT_TRUE(descriptor.m_required);
    EXPECT_FALSE(descriptor.m_defaultValue.has_value());
}

TEST(RuntimeDescriptor, ParameterDescriptorNonDefaultConstruction) {
    const ParameterDescriptor descriptor{"count", ValueType::Integer, "Object count", false,
                                         Value{3}};
    EXPECT_EQ(descriptor.m_name, "count");
    EXPECT_EQ(descriptor.m_type, ValueType::Integer);
    EXPECT_EQ(descriptor.m_description, "Object count");
    EXPECT_FALSE(descriptor.m_required);
    ASSERT_TRUE(descriptor.m_defaultValue.has_value());
    EXPECT_EQ(*descriptor.m_defaultValue, Value{3});
}

TEST(RuntimeDescriptor, ActionDescriptorDefaults) {
    const ActionDescriptor descriptor{};
    EXPECT_TRUE(descriptor.m_name.empty());
    EXPECT_TRUE(descriptor.m_description.empty());
    EXPECT_TRUE(descriptor.m_parameters.empty());
    EXPECT_EQ(descriptor.m_returnType, ValueType::Null);
    EXPECT_TRUE(descriptor.m_returnDescription.empty());
    EXPECT_TRUE(descriptor.m_version.empty());
    EXPECT_TRUE(descriptor.m_tags.empty());
}

TEST(RuntimeDescriptor, ActionDescriptorNonDefaultConstruction) {
    const ParameterDescriptor parameter{"a", ValueType::Number, "First value", true, std::nullopt};
    const ActionDescriptor descriptor{
        "add",     "Add two numbers", {parameter},      ValueType::Number,
        "The sum", "1.0.0",           {"math", "basic"}};
    EXPECT_EQ(descriptor.m_name, "add");
    EXPECT_EQ(descriptor.m_description, "Add two numbers");
    ASSERT_EQ(descriptor.m_parameters.size(), 1U);
    EXPECT_EQ(descriptor.m_parameters[0].m_name, "a");
    EXPECT_EQ(descriptor.m_returnType, ValueType::Number);
    EXPECT_EQ(descriptor.m_returnDescription, "The sum");
    EXPECT_EQ(descriptor.m_version, "1.0.0");
    ASSERT_EQ(descriptor.m_tags.size(), 2U);
    EXPECT_EQ(descriptor.m_tags[0], "math");
    EXPECT_EQ(descriptor.m_tags[1], "basic");
}

TEST(RuntimeDescriptor, ModuleDescriptorDefaults) {
    const ModuleDescriptor descriptor{};
    EXPECT_TRUE(descriptor.m_name.empty());
    EXPECT_TRUE(descriptor.m_description.empty());
    EXPECT_TRUE(descriptor.m_version.empty());
}

TEST(RuntimeDescriptor, ModuleDescriptorNonDefaultConstruction) {
    const ModuleDescriptor descriptor{"math", "Basic math actions", "1.0.0"};
    EXPECT_EQ(descriptor.m_name, "math");
    EXPECT_EQ(descriptor.m_description, "Basic math actions");
    EXPECT_EQ(descriptor.m_version, "1.0.0");
}

TEST(RuntimeValueConverter, BooleanRoundTrip) {
    const Result<bool> result = ValueConverter<bool>::fromValue(Value{true}, "flag");
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value());
    EXPECT_EQ(ValueConverter<bool>::toValue(false), Value{false});
}

TEST(RuntimeValueConverter, BooleanRejectsInteger) {
    const Result<bool> result = ValueConverter<bool>::fromValue(Value{1}, "flag");
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_path, "flag");
}

TEST(RuntimeValueConverter, IntRoundTrip) {
    const Result<int> result = ValueConverter<int>::fromValue(Value{7}, "count");
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 7);
    EXPECT_EQ(ValueConverter<int>::toValue(-3), Value{-3});
}

TEST(RuntimeValueConverter, IntAcceptsBoundaryValues) {
    const Result<int> max_result = ValueConverter<int>::fromValue(
        Value{std::int64_t{std::numeric_limits<int>::max()}}, "count");
    ASSERT_TRUE(max_result.hasValue());
    EXPECT_EQ(max_result.value(), std::numeric_limits<int>::max());

    const Result<int> min_result = ValueConverter<int>::fromValue(
        Value{std::int64_t{std::numeric_limits<int>::min()}}, "count");
    ASSERT_TRUE(min_result.hasValue());
    EXPECT_EQ(min_result.value(), std::numeric_limits<int>::min());
}

TEST(RuntimeValueConverter, IntOutOfRangeReportsTypeMismatch) {
    const Result<int> overflow =
        ValueConverter<int>::fromValue(Value{std::int64_t{5000000000}}, "count");
    ASSERT_FALSE(overflow.hasValue());
    EXPECT_EQ(overflow.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(overflow.error().m_path, "count");

    const Result<int> underflow =
        ValueConverter<int>::fromValue(Value{std::int64_t{-5000000000}}, "count");
    ASSERT_FALSE(underflow.hasValue());
    EXPECT_EQ(underflow.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(underflow.error().m_path, "count");
}

TEST(RuntimeValueConverter, IntRejectsNumber) {
    const Result<int> result = ValueConverter<int>::fromValue(Value{1.5}, "a");
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_path, "a");
}

TEST(RuntimeValueConverter, Int64RoundTrip) {
    const Result<std::int64_t> result =
        ValueConverter<std::int64_t>::fromValue(Value{std::int64_t{9007199254740993}}, "big");
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), std::int64_t{9007199254740993});
    EXPECT_EQ(ValueConverter<std::int64_t>::toValue(std::int64_t{-5}), Value{std::int64_t{-5}});
}

TEST(RuntimeValueConverter, Int64RejectsBoolean) {
    const Result<std::int64_t> result = ValueConverter<std::int64_t>::fromValue(Value{true}, "big");
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_path, "big");
}

TEST(RuntimeValueConverter, DoubleRoundTrip) {
    const Result<double> result = ValueConverter<double>::fromValue(Value{1.25}, "ratio");
    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(result.value(), 1.25);
    EXPECT_EQ(ValueConverter<double>::toValue(-0.5), Value{-0.5});
}

TEST(RuntimeValueConverter, DoubleAcceptsInteger) {
    const Result<double> result = ValueConverter<double>::fromValue(Value{5}, "ratio");
    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(result.value(), 5.0);
}

TEST(RuntimeValueConverter, DoubleRejectsString) {
    const Result<double> result = ValueConverter<double>::fromValue(Value{"1.5"}, "a");
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_message, "Expected number");
    EXPECT_EQ(result.error().m_path, "a");
}

TEST(RuntimeValueConverter, StringRoundTrip) {
    const Result<std::string> result =
        ValueConverter<std::string>::fromValue(Value{"axiom"}, "name");
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "axiom");
    EXPECT_EQ(ValueConverter<std::string>::toValue("axiom"), Value{"axiom"});
}

TEST(RuntimeValueConverter, StringRejectsInteger) {
    const Result<std::string> result = ValueConverter<std::string>::fromValue(Value{3}, "name");
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_path, "name");
}

TEST(RuntimeValueConverter, VectorRoundTrip) {
    const Value items{Value::Array{Value{1}, Value{2.5}}};
    const Result<std::vector<double>> result =
        ValueConverter<std::vector<double>>::fromValue(items, "items");
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 2U);
    EXPECT_DOUBLE_EQ(result.value()[0], 1.0);
    EXPECT_DOUBLE_EQ(result.value()[1], 2.5);
    const std::vector<double> source{1.5, -2.0};
    EXPECT_EQ(ValueConverter<std::vector<double>>::toValue(source),
              (Value{Value::Array{Value{1.5}, Value{-2.0}}}));
}

TEST(RuntimeValueConverter, VectorOfBoolRoundTrip) {
    const Value flags{Value::Array{Value{true}, Value{false}, Value{true}}};
    const Result<std::vector<bool>> result =
        ValueConverter<std::vector<bool>>::fromValue(flags, "flags");
    ASSERT_TRUE(result.hasValue());
    const std::vector<bool> expected{true, false, true};
    EXPECT_EQ(result.value(), expected);
    EXPECT_EQ(ValueConverter<std::vector<bool>>::toValue(expected), flags);
}

TEST(RuntimeValueConverter, VectorRejectsScalar) {
    const Result<std::vector<int>> result =
        ValueConverter<std::vector<int>>::fromValue(Value{1}, "items");
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_path, "items");
}

TEST(RuntimeValueConverter, VectorElementErrorReportsIndexedPath) {
    const Value items{Value::Array{Value{1}, Value{2}, Value{"oops"}}};
    const Result<std::vector<int>> result =
        ValueConverter<std::vector<int>>::fromValue(items, "items");
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_path, "items[2]");
}

TEST(RuntimeValueConverter, ObjectRoundTrip) {
    const Value object{Value::Object{{"a", Value{1}}, {"b", Value{2}}}};
    const Result<std::unordered_map<std::string, int>> result =
        ValueConverter<std::unordered_map<std::string, int>>::fromValue(object, "config");
    ASSERT_TRUE(result.hasValue());
    const std::unordered_map<std::string, int> expected{{"a", 1}, {"b", 2}};
    EXPECT_EQ(result.value(), expected);
    EXPECT_EQ((ValueConverter<std::unordered_map<std::string, int>>::toValue(expected)), object);
}

TEST(RuntimeValueConverter, ObjectRejectsScalar) {
    const Result<std::unordered_map<std::string, int>> result =
        ValueConverter<std::unordered_map<std::string, int>>::fromValue(Value{1}, "config");
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_path, "config");
}

TEST(RuntimeValueConverter, NestedObjectErrorReportsDottedPath) {
    const Value::Object size{{"x", Value{"abc"}}};
    const Value shape{Value::Object{{"size", Value{size}}}};
    using Nested = std::unordered_map<std::string, std::unordered_map<std::string, double>>;
    const Result<Nested> result = ValueConverter<Nested>::fromValue(shape, "shape");
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_path, "shape.size.x");
}

TEST(RuntimeValueConverter, NestedContainersRoundTrip) {
    using Nested = std::unordered_map<std::string, std::vector<int>>;
    const Value data{Value::Object{{"items", Value{Value::Array{Value{1}, Value{2}}}}}};
    const Result<Nested> result = ValueConverter<Nested>::fromValue(data, "data");
    ASSERT_TRUE(result.hasValue());
    const Nested expected{{"items", {1, 2}}};
    EXPECT_EQ(result.value(), expected);
    EXPECT_EQ(ValueConverter<Nested>::toValue(expected), data);
}

TEST(RuntimeValueConverter, NestedContainersErrorReportsCombinedPath) {
    using Nested = std::unordered_map<std::string, std::vector<int>>;
    const Value data{Value::Object{{"items", Value{Value::Array{Value{1}, Value{"oops"}}}}}};
    const Result<Nested> result = ValueConverter<Nested>::fromValue(data, "data");
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_path, "data.items[1]");
}

TEST(RuntimeValueTypeOf, MapsCppTypesToValueTypes) {
    EXPECT_EQ(axiom::runtime::ValueTypeOf<bool>::VALUE, ValueType::Boolean);
    EXPECT_EQ(axiom::runtime::ValueTypeOf<int>::VALUE, ValueType::Integer);
    EXPECT_EQ(axiom::runtime::ValueTypeOf<std::int64_t>::VALUE, ValueType::Integer);
    EXPECT_EQ(axiom::runtime::ValueTypeOf<float>::VALUE, ValueType::Number);
    EXPECT_EQ(axiom::runtime::ValueTypeOf<double>::VALUE, ValueType::Number);
    EXPECT_EQ(axiom::runtime::ValueTypeOf<std::string>::VALUE, ValueType::String);
    EXPECT_EQ(axiom::runtime::ValueTypeOf<const char*>::VALUE, ValueType::String);
    EXPECT_EQ(axiom::runtime::ValueTypeOf<std::vector<int>>::VALUE, ValueType::Array);
    EXPECT_EQ((axiom::runtime::ValueTypeOf<std::unordered_map<std::string, double>>::VALUE),
              ValueType::Object);
    EXPECT_EQ(axiom::runtime::ValueTypeOf<void>::VALUE, ValueType::Null);
}

TEST(RuntimeParamBuilder, ParamBuildsRequiredDescriptor) {
    const ParameterDescriptor descriptor = param<double>("a", "First value");
    EXPECT_EQ(descriptor.m_name, "a");
    EXPECT_EQ(descriptor.m_type, ValueType::Number);
    EXPECT_EQ(descriptor.m_description, "First value");
    EXPECT_TRUE(descriptor.m_required);
    EXPECT_FALSE(descriptor.m_defaultValue.has_value());
}

TEST(RuntimeParamBuilder, OptionalParamBuildsOptionalDescriptor) {
    const ParameterDescriptor descriptor = optionalParam<int>("count", "Object count");
    EXPECT_EQ(descriptor.m_name, "count");
    EXPECT_EQ(descriptor.m_type, ValueType::Integer);
    EXPECT_EQ(descriptor.m_description, "Object count");
    EXPECT_FALSE(descriptor.m_required);
    EXPECT_FALSE(descriptor.m_defaultValue.has_value());
}

TEST(RuntimeParamBuilder, ParamDefaultsDescription) {
    const ParameterDescriptor descriptor = param<std::string>("name");
    EXPECT_EQ(descriptor.m_name, "name");
    EXPECT_EQ(descriptor.m_type, ValueType::String);
    EXPECT_TRUE(descriptor.m_description.empty());
    EXPECT_TRUE(descriptor.m_required);
    EXPECT_FALSE(descriptor.m_defaultValue.has_value());
}

TEST(RuntimeParamBuilder, BuildersMapContainerAndScalarTypes) {
    EXPECT_EQ(param<bool>("flag").m_type, ValueType::Boolean);
    EXPECT_EQ(param<std::int64_t>("size").m_type, ValueType::Integer);
    EXPECT_EQ(param<std::vector<double>>("samples").m_type, ValueType::Array);
    EXPECT_EQ((optionalParam<std::unordered_map<std::string, int>>("labels").m_type),
              ValueType::Object);
    EXPECT_FALSE(optionalParam<bool>("flag").m_required);
}

namespace {

struct ConstFunctor {
    double operator()(double first, double second) const { return first + second; }
};

struct MutableFunctor {
    double operator()(int value) { return static_cast<double>(value); }
};

ActionDescriptor makeAddDescriptor() {
    return ActionDescriptor{.m_name = "math.add",
                            .m_description = "Adds two numbers",
                            .m_parameters = {param<double>("a"), param<double>("b")},
                            .m_returnType = ValueType::Number};
}

} // namespace

TEST(RuntimeFunctionTraits, ExtractsFunctionType) {
    using Traits = FunctionTraits<double(int, std::string)>;
    static_assert(std::is_same_v<Traits::ReturnType, double>);
    static_assert(Traits::ARITY == 2);
    static_assert(std::is_same_v<Traits::ArgsTuple, std::tuple<int, std::string>>);
}

TEST(RuntimeFunctionTraits, ExtractsFunctionPointer) {
    using Traits = FunctionTraits<int (*)(bool)>;
    static_assert(std::is_same_v<Traits::ReturnType, int>);
    static_assert(Traits::ARITY == 1);
    static_assert(std::is_same_v<Traits::ArgsTuple, std::tuple<bool>>);
}

TEST(RuntimeFunctionTraits, ExtractsConstMemberFunctionPointer) {
    using Traits = FunctionTraits<decltype(&ConstFunctor::operator())>;
    static_assert(std::is_same_v<Traits::ReturnType, double>);
    static_assert(Traits::ARITY == 2);
    static_assert(std::is_same_v<Traits::ArgsTuple, std::tuple<double, double>>);
}

TEST(RuntimeFunctionTraits, ExtractsNonConstMemberFunctionPointer) {
    using Traits = FunctionTraits<decltype(&MutableFunctor::operator())>;
    static_assert(std::is_same_v<Traits::ReturnType, double>);
    static_assert(Traits::ARITY == 1);
    static_assert(std::is_same_v<Traits::ArgsTuple, std::tuple<int>>);
}

TEST(RuntimeFunctionTraits, ExtractsStatelessLambda) {
    const auto negate = [](int value) { return -value; };
    using Traits = FunctionTraits<decltype(negate)>;
    static_assert(std::is_same_v<Traits::ReturnType, int>);
    static_assert(Traits::ARITY == 1);
    static_assert(std::is_same_v<Traits::ArgsTuple, std::tuple<int>>);
}

TEST(RuntimeFunctionTraits, ExtractsCapturingLambda) {
    const double offset = 1.5;
    const auto shift = [offset](double value) { return value + offset; };
    using Traits = FunctionTraits<decltype(shift)>;
    static_assert(std::is_same_v<Traits::ReturnType, double>);
    static_assert(Traits::ARITY == 1);
    static_assert(std::is_same_v<Traits::ArgsTuple, std::tuple<double>>);
}

TEST(RuntimeFunctionTraits, ExtractsFunctor) {
    using Traits = FunctionTraits<ConstFunctor>;
    static_assert(std::is_same_v<Traits::ReturnType, double>);
    static_assert(Traits::ARITY == 2);
    static_assert(std::is_same_v<Traits::ArgsTuple, std::tuple<double, double>>);
}

TEST(RuntimeFunctionTraits, ExtractsVoidSignature) {
    using Traits = FunctionTraits<void()>;
    static_assert(std::is_same_v<Traits::ReturnType, void>);
    static_assert(Traits::ARITY == 0);
    static_assert(std::is_same_v<Traits::ArgsTuple, std::tuple<>>);
}

TEST(RuntimeTypedAction, InvokesCallableWithConvertedArguments) {
    const auto add = [](double a, double b) { return a + b; };
    TypedActionAdapter<decltype(add), double, double> action{
        makeAddDescriptor(), add, std::make_tuple(param<double>("a"), param<double>("b"))};

    const Result<Value> result =
        action.invoke(Arguments{{"a", Value{10.0}}, {"b", Value{20.0}}}, InvocationContext{});
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().type(), ValueType::Number);
    EXPECT_DOUBLE_EQ(result.value().asNumber(), 30.0);
}

TEST(RuntimeTypedAction, TypeMismatchReportsParameterPath) {
    const auto add = [](double a, double b) { return a + b; };
    TypedActionAdapter<decltype(add), double, double> action{
        makeAddDescriptor(), add, std::make_tuple(param<double>("a"), param<double>("b"))};

    const Result<Value> result =
        action.invoke(Arguments{{"a", Value{"text"}}, {"b", Value{2.0}}}, InvocationContext{});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_path, "a");
    EXPECT_NE(result.error().m_message.find("number"), std::string::npos);
}

TEST(RuntimeTypedAction, MissingRequiredArgumentReportsParameterPath) {
    const auto add = [](double a, double b) { return a + b; };
    TypedActionAdapter<decltype(add), double, double> action{
        makeAddDescriptor(), add, std::make_tuple(param<double>("a"), param<double>("b"))};

    const Result<Value> result = action.invoke(Arguments{{"a", Value{1.0}}}, InvocationContext{});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::MissingArgument);
    EXPECT_EQ(result.error().m_path, "b");
}

TEST(RuntimeTypedAction, OptionalParameterUsesDefaultValueWhenAbsent) {
    const auto scale = [](double value, double factor) { return value * factor; };
    ParameterDescriptor factor = optionalParam<double>("factor");
    factor.m_defaultValue = Value{2.0};
    ActionDescriptor descriptor{.m_name = "math.scale",
                                .m_parameters = {param<double>("value"), factor},
                                .m_returnType = ValueType::Number};
    TypedActionAdapter<decltype(scale), double, double> action{
        descriptor, scale, std::make_tuple(param<double>("value"), factor)};

    const Result<Value> result =
        action.invoke(Arguments{{"value", Value{12.0}}}, InvocationContext{});
    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(result.value().asNumber(), 24.0);
}

TEST(RuntimeTypedAction, OptionalParameterUsesValueInitializationWithoutDefault) {
    const auto scale = [](double value, double factor) { return value * factor; };
    ActionDescriptor descriptor{
        .m_name = "math.scale",
        .m_parameters = {param<double>("value"), optionalParam<double>("factor")},
        .m_returnType = ValueType::Number};
    TypedActionAdapter<decltype(scale), double, double> action{
        descriptor, scale,
        std::make_tuple(param<double>("value"), optionalParam<double>("factor"))};

    const Result<Value> result =
        action.invoke(Arguments{{"value", Value{12.0}}}, InvocationContext{});
    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(result.value().asNumber(), 0.0);
}

TEST(RuntimeTypedAction, VoidCallableProducesNullValue) {
    int call_count = 0;
    const auto record = [&call_count]() { call_count += 1; };
    ActionDescriptor descriptor{.m_name = "log.record", .m_returnType = ValueType::Null};
    TypedActionAdapter<decltype(record)> action{descriptor, record, std::tuple<>{}};

    const Result<Value> result = action.invoke(Arguments{}, InvocationContext{});
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().isNull());
    EXPECT_EQ(call_count, 1);
}

TEST(RuntimeTypedAction, StdExceptionMapsToInvocationFailed) {
    const auto fail = [](double) -> double { throw std::runtime_error{"boom"}; };
    TypedActionAdapter<decltype(fail), double> action{
        ActionDescriptor{.m_name = "math.fail", .m_returnType = ValueType::Number}, fail,
        std::make_tuple(param<double>("a"))};

    const Result<Value> result = action.invoke(Arguments{{"a", Value{1.0}}}, InvocationContext{});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::InvocationFailed);
    EXPECT_NE(result.error().m_message.find("boom"), std::string::npos);
}

TEST(RuntimeTypedAction, NonStdExceptionMapsToInternalError) {
    const auto fail = [](double) -> double { throw 7; };
    TypedActionAdapter<decltype(fail), double> action{
        ActionDescriptor{.m_name = "math.fail", .m_returnType = ValueType::Number}, fail,
        std::make_tuple(param<double>("a"))};

    const Result<Value> result = action.invoke(Arguments{{"a", Value{1.0}}}, InvocationContext{});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::InternalError);
    EXPECT_EQ(result.error().m_message, "Unknown action failure");
}

TEST(RuntimeTypedAction, DescriptorReturnsRegisteredDescriptor) {
    const auto add = [](double a, double b) { return a + b; };
    const TypedActionAdapter<decltype(add), double, double> action{
        makeAddDescriptor(), add, std::make_tuple(param<double>("a"), param<double>("b"))};

    EXPECT_EQ(action.descriptor().m_name, "math.add");
    EXPECT_EQ(action.descriptor().m_parameters.size(), 2U);
    EXPECT_EQ(action.descriptor().m_returnType, ValueType::Number);
}

TEST(RuntimeTypedAction, FactoryBuildsAdapterFromCallable) {
    const auto add = [](double a, double b) { return a + b; };
    std::unique_ptr<IAction> action = makeTypedAction(
        makeAddDescriptor(), add, std::array{param<double>("a"), param<double>("b")});

    const Result<Value> result =
        action->invoke(Arguments{{"a", Value{1.0}}, {"b", Value{2.0}}}, InvocationContext{});
    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(result.value().asNumber(), 3.0);
    EXPECT_EQ(action->descriptor().m_name, "math.add");
}

TEST(RuntimeTypedAction, FactorySupportsZeroParameterCallable) {
    const auto ping = []() { return std::string{"pong"}; };
    std::unique_ptr<IAction> action = makeTypedAction(
        ActionDescriptor{.m_name = "net.ping", .m_returnType = ValueType::String}, ping);

    const Result<Value> result = action->invoke(Arguments{}, InvocationContext{});
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().asString(), "pong");
}

namespace {

std::unique_ptr<IAction> makeAddAction() {
    const auto add = [](double a, double b) { return a + b; };
    return makeTypedAction(makeAddDescriptor(), add,
                           std::array{param<double>("a"), param<double>("b")});
}

class ThrowingAction final : public IAction {
public:
    explicit ThrowingAction(bool throw_std_exception) : m_throwStdException{throw_std_exception} {}

    [[nodiscard]] const ActionDescriptor& descriptor() const noexcept override {
        return m_descriptor;
    }

    Result<Value> invoke(const Arguments&, const InvocationContext&) override {
        if(m_throwStdException) {
            throw std::runtime_error{"dispatcher boom"};
        }
        throw 7;
    }

private:
    ActionDescriptor m_descriptor{.m_name = "math.fail", .m_returnType = ValueType::Null};
    bool m_throwStdException;
};

} // namespace

TEST(RuntimeRegistry, RegisterAndFindReturnsAction) {
    Registry registry;
    const ActionId id{"math", "add"};
    ASSERT_TRUE(registry.registerAction(id, makeAddAction()).hasValue());
    ASSERT_NE(registry.find(id), nullptr);
    EXPECT_EQ(registry.find(id)->descriptor().m_name, "math.add");

    const Registry& const_registry = registry;
    ASSERT_NE(const_registry.find(id), nullptr);
    EXPECT_EQ(const_registry.find(id)->descriptor().m_name, "math.add");

    const ActionId unknown{"math", "missing"};
    EXPECT_EQ(registry.find(unknown), nullptr);
    EXPECT_EQ(const_registry.find(unknown), nullptr);
}

TEST(RuntimeRegistry, DescribeReturnsActionDescriptor) {
    Registry registry;
    const ActionId id{"math", "add"};
    ASSERT_TRUE(registry.registerAction(id, makeAddAction()).hasValue());

    const ActionDescriptor* descriptor = registry.describe(id);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->m_name, "math.add");
    EXPECT_EQ(descriptor->m_parameters.size(), 2U);
    EXPECT_EQ(registry.describe(ActionId{"math", "missing"}), nullptr);
}

TEST(RuntimeRegistry, ListActionsReturnsSortedIds) {
    Registry registry;
    ASSERT_TRUE(registry.registerAction(ActionId{"zeta", "last"}, makeAddAction()).hasValue());
    ASSERT_TRUE(registry.registerAction(ActionId{"alpha", "second"}, makeAddAction()).hasValue());
    ASSERT_TRUE(registry.registerAction(ActionId{"alpha", "first"}, makeAddAction()).hasValue());

    const std::vector<ActionId> ids = registry.listActions();
    ASSERT_EQ(ids.size(), 3U);
    EXPECT_EQ(ids[0].toString(), "alpha.first");
    EXPECT_EQ(ids[1].toString(), "alpha.second");
    EXPECT_EQ(ids[2].toString(), "zeta.last");

    const std::vector<ActionId> repeated = registry.listActions();
    EXPECT_EQ(repeated, ids);
}

TEST(RuntimeRegistry, ListActionsFiltersByModule) {
    Registry registry;
    ASSERT_TRUE(registry.registerAction(ActionId{"beta", "add"}, makeAddAction()).hasValue());
    ASSERT_TRUE(registry.registerAction(ActionId{"alpha", "second"}, makeAddAction()).hasValue());
    ASSERT_TRUE(registry.registerAction(ActionId{"alpha", "first"}, makeAddAction()).hasValue());

    const std::vector<ActionId> alpha_ids = registry.listActions("alpha");
    ASSERT_EQ(alpha_ids.size(), 2U);
    EXPECT_EQ(alpha_ids[0].toString(), "alpha.first");
    EXPECT_EQ(alpha_ids[1].toString(), "alpha.second");

    EXPECT_TRUE(registry.listActions("gamma").empty());
}

TEST(RuntimeRegistry, DuplicateActionRegistrationRejected) {
    Registry registry;
    const ActionId id{"math", "add"};
    ASSERT_TRUE(registry.registerAction(id, makeAddAction()).hasValue());

    const Result<void> duplicate = registry.registerAction(id, makeAddAction());
    ASSERT_FALSE(duplicate.hasValue());
    EXPECT_EQ(duplicate.error().m_code, ErrorCode::InvalidArgument);
    EXPECT_NE(duplicate.error().m_message.find("math.add"), std::string::npos);
    EXPECT_NE(registry.find(id), nullptr);
}

TEST(RuntimeRegistry, InvalidActionIdRejected) {
    Registry registry;
    const ActionId invalid{"math.add.extra"};
    const Result<void> result = registry.registerAction(invalid, makeAddAction());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::InvalidArgument);
    EXPECT_NE(result.error().m_message.find("math.add.extra"), std::string::npos);
    EXPECT_EQ(registry.find(invalid), nullptr);
}

TEST(RuntimeRegistry, NullActionRejected) {
    Registry registry;
    const Result<void> result = registry.registerAction(ActionId{"math", "add"}, nullptr);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::InvalidArgument);
    EXPECT_EQ(registry.find(ActionId{"math", "add"}), nullptr);
}

TEST(RuntimeRegistry, RegisterModuleAndFindModule) {
    Registry registry;
    ASSERT_TRUE(registry.registerModule(ModuleDescriptor{"math", "Basic math actions", "1.0.0"})
                    .hasValue());

    const ModuleDescriptor* module = registry.findModule("math");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->m_name, "math");
    EXPECT_EQ(module->m_description, "Basic math actions");
    EXPECT_EQ(module->m_version, "1.0.0");
    EXPECT_EQ(registry.findModule("geometry"), nullptr);
}

TEST(RuntimeRegistry, DuplicateModuleRegistrationRejected) {
    Registry registry;
    ASSERT_TRUE(registry.registerModule(ModuleDescriptor{"math", "First", "1.0.0"}).hasValue());

    const Result<void> duplicate =
        registry.registerModule(ModuleDescriptor{"math", "Second", "2.0.0"});
    ASSERT_FALSE(duplicate.hasValue());
    EXPECT_EQ(duplicate.error().m_code, ErrorCode::InvalidArgument);
    EXPECT_NE(duplicate.error().m_message.find("math"), std::string::npos);
    ASSERT_NE(registry.findModule("math"), nullptr);
    EXPECT_EQ(registry.findModule("math")->m_description, "First");
}

TEST(RuntimeDispatcher, UnknownActionReturnsActionNotFound) {
    Registry registry;
    Dispatcher dispatcher{registry};

    const Result<Value> result =
        dispatcher.invoke(ActionId{"math", "add"}, Arguments{}, InvocationContext{});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::ActionNotFound);
    EXPECT_EQ(result.error().m_path, "math.add");
    EXPECT_NE(result.error().m_message.find("math.add"), std::string::npos);
}

TEST(RuntimeDispatcher, MissingRequiredArgumentReportsParameterPath) {
    Registry registry;
    ASSERT_TRUE(registry.registerAction(ActionId{"math", "add"}, makeAddAction()).hasValue());
    Dispatcher dispatcher{registry};

    const Result<Value> result = dispatcher.invoke(
        ActionId{"math", "add"}, Arguments{{"a", Value{1.0}}}, InvocationContext{});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::MissingArgument);
    EXPECT_EQ(result.error().m_path, "b");
}

TEST(RuntimeDispatcher, UnknownArgumentReportsArgumentPath) {
    Registry registry;
    ASSERT_TRUE(registry.registerAction(ActionId{"math", "add"}, makeAddAction()).hasValue());
    Dispatcher dispatcher{registry};

    const Result<Value> result =
        dispatcher.invoke(ActionId{"math", "add"},
                          Arguments{{"a", Value{1.0}}, {"b", Value{2.0}}, {"extra", Value{3.0}}},
                          InvocationContext{});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::InvalidArgument);
    EXPECT_EQ(result.error().m_path, "extra");
}

TEST(RuntimeDispatcher, SuccessPathReturnsActionValue) {
    Registry registry;
    ASSERT_TRUE(registry.registerAction(ActionId{"math", "add"}, makeAddAction()).hasValue());
    Dispatcher dispatcher{registry};

    const Result<Value> result =
        dispatcher.invoke(ActionId{"math", "add"},
                          Arguments{{"a", Value{10.0}}, {"b", Value{20.0}}}, InvocationContext{});
    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(result.value().asNumber(), 30.0);
}

TEST(RuntimeDispatcher, StdExceptionMapsToInvocationFailed) {
    Registry registry;
    ASSERT_TRUE(
        registry.registerAction(ActionId{"math", "fail"}, std::make_unique<ThrowingAction>(true))
            .hasValue());
    Dispatcher dispatcher{registry};

    const Result<Value> result =
        dispatcher.invoke(ActionId{"math", "fail"}, Arguments{}, InvocationContext{});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::InvocationFailed);
    EXPECT_NE(result.error().m_message.find("dispatcher boom"), std::string::npos);
}

TEST(RuntimeDispatcher, NonStdExceptionMapsToInternalError) {
    Registry registry;
    ASSERT_TRUE(
        registry.registerAction(ActionId{"math", "fail"}, std::make_unique<ThrowingAction>(false))
            .hasValue());
    Dispatcher dispatcher{registry};

    const Result<Value> result =
        dispatcher.invoke(ActionId{"math", "fail"}, Arguments{}, InvocationContext{});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::InternalError);
    EXPECT_EQ(result.error().m_message, "Unknown action failure");
}

TEST(RuntimeDispatcher, AbsentOptionalParameterFallsThroughToAdapterDefault) {
    Registry registry;
    const auto scale = [](double value, double factor) { return value * factor; };
    ParameterDescriptor factor = optionalParam<double>("factor");
    factor.m_defaultValue = Value{2.0};
    ActionDescriptor descriptor{.m_name = "math.scale",
                                .m_parameters = {param<double>("value"), factor},
                                .m_returnType = ValueType::Number};
    ASSERT_TRUE(registry
                    .registerAction(ActionId{"math", "scale"},
                                    makeTypedAction(descriptor, scale,
                                                    std::array{param<double>("value"), factor}))
                    .hasValue());
    Dispatcher dispatcher{registry};

    const Result<Value> result = dispatcher.invoke(
        ActionId{"math", "scale"}, Arguments{{"value", Value{12.0}}}, InvocationContext{});
    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(result.value().asNumber(), 24.0);
}

namespace {

using SizeMap = std::unordered_map<std::string, double>;
using ShapeMap = std::unordered_map<std::string, SizeMap>;

double negateValue(double value) { return -value; }

double sumShapeSizes(const ShapeMap& shapes) {
    return std::accumulate(
        shapes.begin(), shapes.end(), 0.0, [](double sum, const ShapeMap::value_type& shape) {
            return sum + std::accumulate(shape.second.begin(), shape.second.end(), 0.0,
                                         [](double inner, const SizeMap::value_type& size) {
                                             return inner + size.second;
                                         });
        });
}

double combineContainerParts(const std::string& label,
                             const std::vector<int>& samples,
                             const SizeMap& weights) {
    const double base = static_cast<double>(label.size());
    const double sample_sum =
        std::accumulate(samples.begin(), samples.end(), 0.0,
                        [](double sum, int sample) { return sum + static_cast<double>(sample); });
    const double weight_sum = std::accumulate(
        weights.begin(), weights.end(), 0.0,
        [](double sum, const SizeMap::value_type& entry) { return sum + entry.second; });
    return base + sample_sum + weight_sum;
}

class CounterService {
public:
    int addAndStore(int delta) {
        m_total += delta;
        return m_total;
    }

    int total() const { return m_total; }

private:
    int m_total = 0;
};

class ContextRecordingAction final : public IAction {
public:
    [[nodiscard]] const ActionDescriptor& descriptor() const noexcept override {
        return m_descriptor;
    }

    Result<Value> invoke(const Arguments&, const InvocationContext& context) override {
        m_captured = context;
        return Result<Value>{Value{}};
    }

    InvocationContext m_captured;

private:
    ActionDescriptor m_descriptor{.m_name = "probe.record", .m_returnType = ValueType::Null};
};

ModuleBuilder registerAddAction(Runtime& runtime) {
    ModuleBuilder math = runtime.module("math", "Basic mathematical operations");
    const Result<void> registered = math.action(
        "add", "Add two numbers", [](double first, double second) { return first + second; },
        param<double>("a", "First value"), param<double>("b", "Second value"));
    EXPECT_TRUE(registered.hasValue());
    return math;
}

} // namespace

TEST(RuntimeEndToEnd, RegistersAndInvokesAddAction) {
    Runtime runtime;
    registerAddAction(runtime);

    const Result<Value> result =
        runtime.invoke("math.add", Arguments{{"a", Value{10}}, {"b", Value{20}}});
    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(result.value().asNumber(), 30.0);
}

TEST(RuntimeEndToEnd, StringReturningActionRoundTrips) {
    Runtime runtime;
    ModuleBuilder text = runtime.module("text");
    ASSERT_TRUE(text.action(
                        "greet", "Greet a person",
                        [](std::string name) { return "Hello, " + std::move(name) + "!"; },
                        param<std::string>("name"))
                    .hasValue());

    const Result<Value> result = runtime.invoke("text.greet", Arguments{{"name", Value{"world"}}});
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().isString());
    EXPECT_EQ(result.value().asString(), "Hello, world!");
}

TEST(RuntimeEndToEnd, VoidActionExecutesAndReturnsNull) {
    Runtime runtime;
    bool executed = false;
    ModuleBuilder jobs = runtime.module("jobs");
    ASSERT_TRUE(jobs.action("run", "Run the job", [&executed]() { executed = true; }).hasValue());

    const Result<Value> result = runtime.invoke("jobs.run");
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().isNull());
    EXPECT_TRUE(executed);
}

TEST(RuntimeEndToEnd, OptionalParameterDefaultIsUsedWhenAbsent) {
    Runtime runtime;
    ModuleBuilder math = runtime.module("math");
    ParameterDescriptor factor = optionalParam<double>("factor", "Scale factor");
    factor.m_defaultValue = Value{3.0};
    ASSERT_TRUE(math.action(
                        "scale", "Scale a value",
                        [](double value, double scale) { return value * scale; },
                        param<double>("value"), factor)
                    .hasValue());

    const Result<Value> result = runtime.invoke("math.scale", Arguments{{"value", Value{4.0}}});
    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(result.value().asNumber(), 12.0);
}

TEST(RuntimeEndToEnd, NestedMapArgumentConvertsEndToEnd) {
    Runtime runtime;
    ModuleBuilder geometry = runtime.module("geometry");
    ASSERT_TRUE(geometry
                    .action(
                        "sum_sizes", "Sum all size values of all shapes",
                        [](ShapeMap shapes) { return sumShapeSizes(std::move(shapes)); },
                        param<ShapeMap>("shapes"))
                    .hasValue());

    const Value shapes{
        Value::Object{{"box", Value{Value::Object{{"w", Value{2.0}}, {"h", Value{3.0}}}}},
                      {"sphere", Value{Value::Object{{"r", Value{1.5}}}}}}};
    const Result<Value> result =
        runtime.invoke("geometry.sum_sizes", Arguments{{"shapes", shapes}});
    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(result.value().asNumber(), 6.5);
}

TEST(RuntimeEndToEnd, FunctionPointerRegistrationInvokes) {
    Runtime runtime;
    ModuleBuilder math = runtime.module("math");
    ASSERT_TRUE(
        math.action("negate", "Negate a number", &negateValue, param<double>("value")).hasValue());

    const Result<Value> result = runtime.invoke("math.negate", Arguments{{"value", Value{7.5}}});
    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(result.value().asNumber(), -7.5);
}

TEST(RuntimeEndToEnd, MemberFunctionRegistrationMutatesInstance) {
    Runtime runtime;
    CounterService service;
    ModuleBuilder counter = runtime.module("counter");
    ASSERT_TRUE(counter
                    .action("add", "Add a delta to the stored total", service,
                            &CounterService::addAndStore, param<int>("delta"))
                    .hasValue());

    const Result<Value> result = runtime.invoke("counter.add", Arguments{{"delta", Value{5}}});
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().asInteger(), 5);
    EXPECT_EQ(service.total(), 5);

    const Result<Value> second = runtime.invoke("counter.add", Arguments{{"delta", Value{3}}});
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(second.value().asInteger(), 8);
    EXPECT_EQ(service.total(), 8);
}

TEST(RuntimeEndToEnd, ConstMemberFunctionRegistrationReadsInstance) {
    Runtime runtime;
    const CounterService service;
    ModuleBuilder counter = runtime.module("counter");
    ASSERT_TRUE(counter.action("total", "Report the stored total", service, &CounterService::total)
                    .hasValue());

    const Result<Value> result = runtime.invoke("counter.total");
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().asInteger(), 0);
}

TEST(RuntimeEndToEnd, SpecErrorPathExampleReportsDottedPath) {
    Runtime runtime;
    ModuleBuilder geometry = runtime.module("geometry");
    ASSERT_TRUE(geometry
                    .action(
                        "measure", "Sum all size values of a shape",
                        [](ShapeMap shape) { return sumShapeSizes(std::move(shape)); },
                        param<ShapeMap>("shape"))
                    .hasValue());

    const Value shape{Value::Object{{"size", Value{Value::Object{{"x", Value{"abc"}}}}}}};
    const Result<Value> result = runtime.invoke("geometry.measure", Arguments{{"shape", shape}});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_message, "Expected number");
    EXPECT_EQ(result.error().m_path, "shape.size.x");
}

TEST(RuntimeEndToEnd, MixedModulesListAndInvokeAcrossModules) {
    Runtime runtime;
    ModuleBuilder math = runtime.module("math", "Numeric actions");
    ModuleBuilder text = runtime.module("text", "Text actions");
    ASSERT_TRUE(math.action(
                        "add", "Add two numbers",
                        [](double first, double second) { return first + second; },
                        param<double>("a"), param<double>("b"))
                    .hasValue());
    ASSERT_TRUE(math.action(
                        "negate", "Negate a number", [](double value) { return -value; },
                        param<double>("value"))
                    .hasValue());
    ASSERT_TRUE(text.action(
                        "echo", "Echo the input",
                        [](std::string input) { return std::move(input); },
                        param<std::string>("input"))
                    .hasValue());
    ASSERT_TRUE(text.action(
                        "shout", "Append an exclamation mark",
                        [](std::string input) { return std::move(input) + "!"; },
                        param<std::string>("input"))
                    .hasValue());

    const std::vector<ActionId> all = runtime.listActions();
    ASSERT_EQ(all.size(), 4U);
    EXPECT_EQ(all[0].toString(), "math.add");
    EXPECT_EQ(all[1].toString(), "math.negate");
    EXPECT_EQ(all[2].toString(), "text.echo");
    EXPECT_EQ(all[3].toString(), "text.shout");

    const std::vector<ActionId> math_only = runtime.listActions("math");
    ASSERT_EQ(math_only.size(), 2U);
    EXPECT_EQ(math_only[0].toString(), "math.add");
    EXPECT_EQ(math_only[1].toString(), "math.negate");

    const std::vector<ActionId> text_only = runtime.listActions("text");
    ASSERT_EQ(text_only.size(), 2U);
    EXPECT_EQ(text_only[0].toString(), "text.echo");
    EXPECT_EQ(text_only[1].toString(), "text.shout");

    const Result<Value> sum =
        runtime.invoke("math.add", Arguments{{"a", Value{10.0}}, {"b", Value{20.0}}});
    ASSERT_TRUE(sum.hasValue());
    EXPECT_DOUBLE_EQ(sum.value().asNumber(), 30.0);

    const Result<Value> negated = runtime.invoke("math.negate", Arguments{{"value", Value{4.5}}});
    ASSERT_TRUE(negated.hasValue());
    EXPECT_DOUBLE_EQ(negated.value().asNumber(), -4.5);

    const Result<Value> echoed = runtime.invoke("text.echo", Arguments{{"input", Value{"hi"}}});
    ASSERT_TRUE(echoed.hasValue());
    EXPECT_EQ(echoed.value().asString(), "hi");

    const Result<Value> shouted = runtime.invoke("text.shout", Arguments{{"input", Value{"hi"}}});
    ASSERT_TRUE(shouted.hasValue());
    EXPECT_EQ(shouted.value().asString(), "hi!");
}

TEST(RuntimeEndToEnd, MultiParameterMixedTypesRoundTrip) {
    Runtime runtime;
    ModuleBuilder stats = runtime.module("stats");
    const auto combine = [](bool flag, int count, double ratio, std::string label,
                            std::vector<int> samples, SizeMap weights) {
        const double base = (flag ? 1.0 : 0.0) + static_cast<double>(count) + ratio;
        return base +
               combineContainerParts(std::move(label), std::move(samples), std::move(weights));
    };
    ASSERT_TRUE(stats
                    .action("combine", "Combine mixed-type inputs into one number", combine,
                            param<bool>("flag"), param<int>("count"), param<double>("ratio"),
                            param<std::string>("label"), param<std::vector<int>>("samples"),
                            param<SizeMap>("weights"))
                    .hasValue());

    const Result<Value> result = runtime.invoke(
        "stats.combine",
        Arguments{{"flag", Value{true}},
                  {"count", Value{3}},
                  {"ratio", Value{2.5}},
                  {"label", Value{"ab"}},
                  {"samples", Value{Value::Array{Value{1}, Value{2}, Value{3}}}},
                  {"weights", Value{Value::Object{{"x", Value{1.5}}, {"y", Value{0.5}}}}}});
    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(result.value().asNumber(), 16.5);
}

TEST(RuntimeFacade, DescribeReturnsRegisteredDescriptor) {
    Runtime runtime;
    registerAddAction(runtime);

    const ActionDescriptor* descriptor = runtime.describe("math.add");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->m_name, "add");
    EXPECT_EQ(descriptor->m_description, "Add two numbers");
    ASSERT_EQ(descriptor->m_parameters.size(), 2U);
    EXPECT_EQ(descriptor->m_parameters[0].m_name, "a");
    EXPECT_EQ(descriptor->m_parameters[1].m_name, "b");
    EXPECT_TRUE(descriptor->m_parameters[0].m_required);
    EXPECT_EQ(descriptor->m_returnType, ValueType::Number);
}

TEST(RuntimeFacade, DescribeRejectsUnknownAndMalformedIds) {
    Runtime runtime;
    registerAddAction(runtime);

    EXPECT_EQ(runtime.describe("math.unknown"), nullptr);
    EXPECT_EQ(runtime.describe("no-dot"), nullptr);
    EXPECT_EQ(runtime.describe(""), nullptr);
}

TEST(RuntimeFacade, ListActionsGlobalAndModuleFiltered) {
    Runtime runtime;
    ModuleBuilder math = runtime.module("math");
    ModuleBuilder text = runtime.module("text");
    ASSERT_TRUE(math.action(
                        "negate", "Negate a number", [](double value) { return -value; },
                        param<double>("value"))
                    .hasValue());
    ASSERT_TRUE(math.action(
                        "add", "Add two numbers",
                        [](double first, double second) { return first + second; },
                        param<double>("a"), param<double>("b"))
                    .hasValue());
    ASSERT_TRUE(text.action(
                        "echo", "Echo the input",
                        [](std::string input) { return std::move(input); },
                        param<std::string>("input"))
                    .hasValue());

    const std::vector<ActionId> all = runtime.listActions();
    ASSERT_EQ(all.size(), 3U);
    EXPECT_EQ(all[0].toString(), "math.add");
    EXPECT_EQ(all[1].toString(), "math.negate");
    EXPECT_EQ(all[2].toString(), "text.echo");

    const std::vector<ActionId> math_only = runtime.listActions("math");
    ASSERT_EQ(math_only.size(), 2U);
    EXPECT_EQ(math_only[0].toString(), "math.add");
    EXPECT_EQ(math_only[1].toString(), "math.negate");
    EXPECT_TRUE(runtime.listActions("absent").empty());
}

TEST(RuntimeFacade, InvokeUnknownActionReportsActionNotFound) {
    Runtime runtime;

    const Result<Value> result = runtime.invoke("math.add");
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::ActionNotFound);
    EXPECT_EQ(result.error().m_path, "math.add");
}

TEST(RuntimeFacade, InvokeMalformedIdReportsInvalidArgument) {
    Runtime runtime;

    const Result<Value> result = runtime.invoke("no-dot");
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::InvalidArgument);
    EXPECT_EQ(result.error().m_path, "no-dot");
}

TEST(RuntimeFacade, InvokeMissingRequiredReportsMissingArgument) {
    Runtime runtime;
    registerAddAction(runtime);

    const Result<Value> result = runtime.invoke("math.add", Arguments{{"a", Value{1.0}}});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::MissingArgument);
    EXPECT_EQ(result.error().m_path, "b");
}

TEST(RuntimeFacade, InvokeUnknownArgumentReportsInvalidArgument) {
    Runtime runtime;
    registerAddAction(runtime);

    const Result<Value> result = runtime.invoke(
        "math.add", Arguments{{"a", Value{1.0}}, {"b", Value{2.0}}, {"c", Value{3.0}}});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::InvalidArgument);
    EXPECT_EQ(result.error().m_path, "c");
}

TEST(RuntimeFacade, InvokeTypeMismatchReportsParameterPath) {
    Runtime runtime;
    registerAddAction(runtime);

    const Result<Value> result =
        runtime.invoke("math.add", Arguments{{"a", Value{"abc"}}, {"b", Value{2.0}}});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_path, "a");
}

TEST(RuntimeFacade, InvokeReportsNestedTypeMismatchPath) {
    Runtime runtime;
    ModuleBuilder geometry = runtime.module("geometry");
    ASSERT_TRUE(
        geometry
            .action(
                "sum_sizes", "Sum all size values",
                [](ShapeMap shapes) { return static_cast<double>(std::move(shapes).size()); },
                param<ShapeMap>("shapes"))
            .hasValue());

    const Value shapes{Value::Object{{"size", Value{Value::Object{{"x", Value{"abc"}}}}}}};
    const Result<Value> result =
        runtime.invoke("geometry.sum_sizes", Arguments{{"shapes", shapes}});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::TypeMismatch);
    EXPECT_EQ(result.error().m_path, "shapes.size.x");
}

TEST(RuntimeFacade, DuplicateActionRegistrationReportsError) {
    Runtime runtime;
    ModuleBuilder math = runtime.module("math");
    ASSERT_TRUE(math.action(
                        "add", "Add", [](double value) { return value; }, param<double>("a"))
                    .hasValue());

    const Result<void> duplicate =
        math.action("add", "Add again", [](double value) { return value; }, param<double>("a"));
    ASSERT_FALSE(duplicate.hasValue());
    EXPECT_EQ(duplicate.error().m_code, ErrorCode::InvalidArgument);
    EXPECT_NE(duplicate.error().m_message.find("already registered"), std::string::npos);
    EXPECT_EQ(duplicate.error().m_path, "math.add");
}

TEST(RuntimeFacade, ModuleRegistrationIsIdempotentOnDuplicate) {
    Runtime runtime;
    ModuleBuilder first = runtime.module("math", "First description");
    ModuleBuilder second = runtime.module("math", "Second description");
    ASSERT_TRUE(
        first.action(
                 "one", "First action", [](double value) { return value; }, param<double>("a"))
            .hasValue());
    ASSERT_TRUE(
        second
            .action(
                "two", "Second action", [](double value) { return value; }, param<double>("a"))
            .hasValue());

    const std::vector<ActionId> ids = runtime.listActions("math");
    ASSERT_EQ(ids.size(), 2U);
    EXPECT_EQ(ids[0].toString(), "math.one");
    EXPECT_EQ(ids[1].toString(), "math.two");
}

TEST(RuntimeFacade, ActionThrowingStdExceptionMapsToInvocationFailed) {
    Runtime runtime;
    ModuleBuilder math = runtime.module("math");
    ASSERT_TRUE(math.action(
                        "fail", "Always throws",
                        [](double) -> double { throw std::runtime_error{"facade boom"}; },
                        param<double>("a"))
                    .hasValue());

    const Result<Value> result = runtime.invoke("math.fail", Arguments{{"a", Value{1.0}}});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::InvocationFailed);
    EXPECT_NE(result.error().m_message.find("facade boom"), std::string::npos);
}

TEST(RuntimeFacade, ActionThrowingNonStdExceptionMapsToInternalError) {
    Runtime runtime;
    ModuleBuilder math = runtime.module("math");
    ASSERT_TRUE(
        math.action(
                "fail", "Always throws", [](double) -> double { throw 7; }, param<double>("a"))
            .hasValue());

    const Result<Value> result = runtime.invoke("math.fail", Arguments{{"a", Value{1.0}}});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().m_code, ErrorCode::InternalError);
    EXPECT_EQ(result.error().m_message, "Unknown action failure");
}

TEST(RuntimeDispatcher, InvocationContextArrivesIntactAtAction) {
    Registry registry;
    auto action = std::make_unique<ContextRecordingAction>();
    ContextRecordingAction* recorder = action.get();
    ASSERT_TRUE(registry.registerAction(ActionId{"probe", "record"}, std::move(action)).hasValue());
    Dispatcher dispatcher{registry};

    InvocationContext context;
    context.m_requestId = "req-1";
    context.m_traceId = "trace-2";
    context.m_caller = "test";
    context.m_metadata = {{"source", Value{"unit"}}};

    const Result<Value> result =
        dispatcher.invoke(ActionId{"probe", "record"}, Arguments{}, context);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(recorder->m_captured.m_requestId, "req-1");
    EXPECT_EQ(recorder->m_captured.m_traceId, "trace-2");
    EXPECT_EQ(recorder->m_captured.m_caller, "test");
    EXPECT_EQ(recorder->m_captured.m_metadata.at("source"), Value{"unit"});
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
