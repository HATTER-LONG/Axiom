#include <axiom/action/detail/value_converter.hpp>
#include <axiom/action/module_builder.hpp>
#include <axiom/foundation/detail/value_path.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/value.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using axiom::Value;
using axiom::detail::appendObjectPath;
using axiom::detail::fromValue;
using axiom::detail::toValue;
using axiom::detail::ValueConvertible;

struct UnsupportedObject {};

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

struct NonDefaultConstructibleIntAllocator {
    // NOLINTNEXTLINE(readability-identifier-naming): allocator_traits mandates value_type.
    using value_type = int;

    explicit NonDefaultConstructibleIntAllocator(const int tag) : tag(tag) {}
    [[nodiscard]] static int* allocate(const std::size_t count) {
        return std::allocator<int>{}.allocate(count);
    }
    static void deallocate(int* const pointer, const std::size_t count) {
        std::allocator<int>{}.deallocate(pointer, count);
    }
    [[nodiscard]] bool operator==(const NonDefaultConstructibleIntAllocator& other) const noexcept {
        static_cast<void>(other);
        return true;
    }

    int tag;
};

static_assert(ValueConvertible<bool>);
static_assert(ValueConvertible<std::int8_t>);
static_assert(ValueConvertible<std::int16_t>);
static_assert(ValueConvertible<std::int32_t>);
static_assert(ValueConvertible<std::int64_t>);
static_assert(ValueConvertible<float>);
static_assert(ValueConvertible<double>);
static_assert(ValueConvertible<std::string>);
static_assert(ValueConvertible<std::vector<std::int32_t>>);
static_assert(ValueConvertible<std::map<std::string, std::vector<double>>>);
static_assert(ValueConvertible<std::unordered_map<std::string, bool>>);
static_assert(!ValueConvertible<UnsupportedObject>);
static_assert(!ValueConvertible<std::map<std::string, int, NonDefaultConstructibleCompare>>);
static_assert(!ValueConvertible<std::unordered_map<std::string, int, NonDefaultConstructibleHash>>);
static_assert(!ValueConvertible<std::vector<int, NonDefaultConstructibleIntAllocator>>);
static_assert(
    !ValueConvertible<std::vector<std::map<std::string, int, NonDefaultConstructibleCompare>>>);

TEST(ValueConverter, SharesDefinitionsWithPublicCallableRegistration) {
    axiom::ModuleBuilder builder{{.namespace_name = "conversion",
                                  .description = {},
                                  .version = {},
                                  .tags = {},
                                  .metadata = {}}};
    const auto registered =
        builder.add("echo", "Echo", [](int value) { return value; }, axiom::param("value"));
    EXPECT_TRUE(registered);
    auto converted = fromValue<int>(Value{42}, "value");
    ASSERT_TRUE(converted);
    EXPECT_EQ(converted.value(), 42);
}

} // namespace

TEST(ValueConverter, ConvertsSupportedScalarValuesInBothDirections) {
    const auto boolean = fromValue<bool>(Value{true}, "enabled");
    const auto integer = fromValue<std::int32_t>(Value{std::int64_t{42}}, "size");
    const auto number = fromValue<double>(Value{std::int64_t{42}}, "ratio");
    const auto text = fromValue<std::string>(Value{"Axiom"}, "name");

    ASSERT_TRUE(boolean);
    ASSERT_TRUE(integer);
    ASSERT_TRUE(number);
    ASSERT_TRUE(text);
    EXPECT_TRUE(boolean.value());
    EXPECT_EQ(integer.value(), 42);
    EXPECT_DOUBLE_EQ(number.value(), 42.0);
    EXPECT_EQ(text.value(), "Axiom");
    EXPECT_EQ(toValue(std::int16_t{5}).asInteger(), 5);
    EXPECT_DOUBLE_EQ(toValue(1.25F).asNumber(), 1.25);
    EXPECT_EQ(toValue(std::string{"text"}).asString(), "text");
}

TEST(ValueConverter, RejectsLossyAndImplicitScalarConversionsAtTheInputPath) {
    const auto number_to_integer = fromValue<int>(Value{2.0}, "size");
    const auto string_to_integer = fromValue<int>(Value{"2"}, "size");
    const auto overflowing_integer = fromValue<std::int8_t>(
        Value{std::int64_t{std::numeric_limits<std::int8_t>::max()} + 1}, "size");

    ASSERT_FALSE(number_to_integer);
    ASSERT_FALSE(string_to_integer);
    ASSERT_FALSE(overflowing_integer);
    EXPECT_EQ(number_to_integer.error().code, axiom::ErrorCode::TypeMismatch);
    EXPECT_EQ(string_to_integer.error().code, axiom::ErrorCode::TypeMismatch);
    EXPECT_EQ(overflowing_integer.error().code, axiom::ErrorCode::TypeMismatch);
    const auto& number_to_integer_path = number_to_integer.error().path;
    const auto& string_to_integer_path = string_to_integer.error().path;
    const auto& overflowing_integer_path = overflowing_integer.error().path;
    ASSERT_TRUE(number_to_integer_path.has_value());
    ASSERT_TRUE(string_to_integer_path.has_value());
    ASSERT_TRUE(overflowing_integer_path.has_value());
    if(!number_to_integer_path.has_value() || !string_to_integer_path.has_value() ||
       !overflowing_integer_path.has_value()) {
        return;
    }
    EXPECT_EQ(number_to_integer_path.value(), "size");
    EXPECT_EQ(string_to_integer_path.value(), "size");
    EXPECT_EQ(overflowing_integer_path.value(), "size");
}

TEST(ValueConverter, AcceptsBothInclusiveBoundsOfNarrowIntegerTypes) {
    const auto lowest = fromValue<std::int8_t>(
        Value{std::int64_t{std::numeric_limits<std::int8_t>::lowest()}}, "minimum");
    const auto highest = fromValue<std::int8_t>(
        Value{std::int64_t{std::numeric_limits<std::int8_t>::max()}}, "maximum");

    ASSERT_TRUE(lowest);
    ASSERT_TRUE(highest);
    EXPECT_EQ(lowest.value(), std::numeric_limits<std::int8_t>::lowest());
    EXPECT_EQ(highest.value(), std::numeric_limits<std::int8_t>::max());
}

TEST(ValueConverter, PreservesPreciseNestedArrayAndObjectPaths) {
    const Value input{Value::Object{{
        "size",
        Value{Value::Object{{
            {"x", Value{Value::Array{Value{std::int64_t{1}}}}},
            {"y", Value{Value::Array{Value{std::int64_t{2}}, Value{"invalid"}}}},
        }}},
    }}};

    const auto converted =
        fromValue<std::map<std::string, std::map<std::string, std::vector<std::int32_t>>>>(input,
                                                                                           "shape");

    ASSERT_FALSE(converted);
    EXPECT_EQ(converted.error().code, axiom::ErrorCode::TypeMismatch);
    const auto& path = converted.error().path;
    ASSERT_TRUE(path.has_value());
    if(!path.has_value()) {
        return;
    }
    EXPECT_EQ(path.value(), "shape.size.y[1]");
}

TEST(ValueConverter, EncodesNonIdentifierMapKeysWithoutAmbiguousPathSyntax) {
    const auto dotted = fromValue<std::map<std::string, bool>>(
        Value{Value::Object{{"a.b", Value{"invalid"}}}}, "shape");
    const auto bracketed = fromValue<std::map<std::string, bool>>(
        Value{Value::Object{{"items[0]", Value{"invalid"}}}}, "shape");
    const auto quoted = fromValue<std::map<std::string, bool>>(
        Value{Value::Object{{"a\"b\\c", Value{"invalid"}}}}, "shape");
    const auto newline = fromValue<std::map<std::string, bool>>(
        Value{Value::Object{{"line\nbreak", Value{"invalid"}}}}, "shape");

    ASSERT_FALSE(dotted);
    ASSERT_FALSE(bracketed);
    ASSERT_FALSE(quoted);
    ASSERT_FALSE(newline);
    const auto& dotted_path = dotted.error().path;
    const auto& bracketed_path = bracketed.error().path;
    const auto& quoted_path = quoted.error().path;
    const auto& newline_path = newline.error().path;
    ASSERT_TRUE(dotted_path.has_value());
    ASSERT_TRUE(bracketed_path.has_value());
    ASSERT_TRUE(quoted_path.has_value());
    ASSERT_TRUE(newline_path.has_value());
    if(!dotted_path.has_value() || !bracketed_path.has_value() || !quoted_path.has_value() ||
       !newline_path.has_value()) {
        return;
    }
    EXPECT_EQ(dotted_path.value(), "shape[\"a.b\"]");
    EXPECT_EQ(bracketed_path.value(), "shape[\"items[0]\"]");
    EXPECT_EQ(quoted_path.value(), "shape[\"a\\\"b\\\\c\"]");
    EXPECT_EQ(newline_path.value(), "shape[\"line\\nbreak\"]");
}

TEST(ValueConverter, KeepsIdentifierObjectPathSegmentsInTheEstablishedDotForm) {
    EXPECT_EQ(appendObjectPath("shape", "size"), "shape.size");
    EXPECT_EQ(appendObjectPath("", "size"), "size");
    EXPECT_EQ(appendObjectPath("shape", "_private2"), "shape._private2");
    EXPECT_EQ(appendObjectPath("shape", "A"), "shape.A");
    EXPECT_EQ(appendObjectPath("shape", "Z9"), "shape.Z9");
    EXPECT_EQ(appendObjectPath("shape", "a0"), "shape.a0");
    EXPECT_EQ(appendObjectPath("shape", ""), "shape[\"\"]");
    EXPECT_EQ(appendObjectPath("shape", "\b\f\r\t\x01\x7f"),
              "shape[\"\\b\\f\\r\\t\\u0001\\u007F\"]");
}

TEST(ValueConverter, LeavesSpaceUnescapedInQuotedObjectPathKeys) {
    EXPECT_EQ(appendObjectPath("shape", "two words"), "shape[\"two words\"]");
}

TEST(ValueConverter, MaterializesNestedContainersAsStableValueShapes) {
    const std::unordered_map<std::string, std::vector<std::int32_t>> source{
        {"zeta", {3, 4}},
        {"alpha", {1, 2}},
    };

    const Value converted = toValue(source);

    ASSERT_TRUE(converted.isObject());
    EXPECT_EQ(converted.asObject().begin()->first, "alpha");
    EXPECT_EQ(converted.asObject().at("alpha").asArray()[1].asInteger(), 2);
    EXPECT_EQ(converted.asObject().at("zeta").asArray()[0].asInteger(), 3);
}

TEST(ValueConverter, RejectsMismatchedContainerShapesAtTheTopLevelPath) {
    const auto array = fromValue<std::vector<bool>>(Value{Value::Object{}}, "points");
    const auto object = fromValue<std::map<std::string, bool>>(Value{Value::Array{}}, "shape");

    ASSERT_FALSE(array);
    ASSERT_FALSE(object);
    EXPECT_EQ(array.error().code, axiom::ErrorCode::TypeMismatch);
    EXPECT_EQ(object.error().code, axiom::ErrorCode::TypeMismatch);
    const auto& array_path = array.error().path;
    const auto& object_path = object.error().path;
    ASSERT_TRUE(array_path.has_value());
    ASSERT_TRUE(object_path.has_value());
    if(!array_path.has_value() || !object_path.has_value()) {
        return;
    }
    EXPECT_EQ(array_path.value(), "points");
    EXPECT_EQ(object_path.value(), "shape");
}

TEST(ValueConverter, ConvertsAndRejectsEveryScalarInputCategory) {
    const auto floating = fromValue<double>(Value{1.5}, "ratio");
    const auto invalid_floating = fromValue<double>(Value{"one"}, "ratio");
    const auto invalid_small_integer = fromValue<std::int8_t>(Value{"one"}, "count");

    ASSERT_TRUE(floating);
    ASSERT_FALSE(invalid_floating);
    ASSERT_FALSE(invalid_small_integer);
    EXPECT_DOUBLE_EQ(floating.value(), 1.5);
    EXPECT_EQ(invalid_floating.error().code, axiom::ErrorCode::TypeMismatch);
    EXPECT_EQ(invalid_small_integer.error().code, axiom::ErrorCode::TypeMismatch);
}

TEST(ValueConverter, ConvertsSuccessfulVectorAndMapContainers) {
    const auto vector = fromValue<std::vector<std::int32_t>>(
        Value{Value::Array{Value{std::int64_t{1}}, Value{std::int64_t{2}}}}, "values");
    const auto map = fromValue<std::map<std::string, bool>>(
        Value{Value::Object{{"enabled", Value{true}}}}, "settings");

    ASSERT_TRUE(vector);
    ASSERT_TRUE(map);
    EXPECT_EQ(vector.value(), (std::vector<std::int32_t>{1, 2}));
    EXPECT_TRUE(map.value().at("enabled"));
}

TEST(ValueConverter, PreservesSuccessfulAndRejectedSpecializedContainerConversions) {
    const auto invalid_string = fromValue<std::string>(Value{true}, "name");
    const auto bytes =
        fromValue<std::vector<std::int8_t>>(Value{Value::Array{Value{std::int64_t{1}}}}, "bytes");
    const auto nested = fromValue<std::map<std::string, std::vector<std::int32_t>>>(
        Value{Value::Object{{"items", Value{Value::Array{Value{std::int64_t{1}}}}}}}, "shape");

    ASSERT_FALSE(invalid_string);
    ASSERT_TRUE(bytes);
    ASSERT_TRUE(nested);
    EXPECT_EQ(invalid_string.error().code, axiom::ErrorCode::TypeMismatch);
    EXPECT_EQ(bytes.value(), (std::vector<std::int8_t>{1}));
    EXPECT_EQ(nested.value().at("items"), (std::vector<std::int32_t>{1}));
}

TEST(ValueConverter, ConvertsEveryArrayElementAndPreservesItsFailureIndex) {
    const auto converted = fromValue<std::vector<std::int32_t>>(
        Value{Value::Array{Value{std::int64_t{1}}, Value{"invalid"}}}, "points");

    ASSERT_FALSE(converted);
    const auto& path = converted.error().path;
    ASSERT_TRUE(path.has_value());
    if(!path.has_value()) {
        return;
    }
    EXPECT_EQ(path.value(), "points[1]");
}
