#pragma once

/** @file value_converter.hpp
 * @brief Shared implementation of the typed Action value boundary.
 */

#include <axiom/foundation/detail/value_path.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace axiom::detail {

namespace value_converter_detail {

// Compatibility aliases for the value-path owner that moved to Foundation; keep
// the historical converter-detail names working for existing consumers.
using ::axiom::detail::commonEscapeSequence;
using ::axiom::detail::escapePathKey;
using ::axiom::detail::isIdentifierPathSegment;
using ::axiom::detail::typeMismatch;
using ::axiom::detail::valueTypeName;

template <typename T> struct IsVector : std::false_type {};

template <typename Element, typename Allocator>
struct IsVector<std::vector<Element, Allocator>> : std::true_type {
    using ElementType = Element;
};

template <typename T> struct IsStringMap : std::false_type {};

template <typename Element, typename Compare, typename Allocator>
struct IsStringMap<std::map<std::string, Element, Compare, Allocator>> : std::true_type {
    using ElementType = Element;
};

template <typename Element, typename Hash, typename Equal, typename Allocator>
struct IsStringMap<std::unordered_map<std::string, Element, Hash, Equal, Allocator>>
    : std::true_type {
    using ElementType = Element;
};

template <typename T>
inline constexpr bool is_signed_integer =
    std::is_integral_v<T> && std::is_signed_v<T> && !std::is_same_v<T, bool>;

template <typename T>
struct IsValueConvertible
    : std::bool_constant<std::is_same_v<T, bool> || is_signed_integer<T> ||
                         std::is_floating_point_v<T> || std::is_same_v<T, std::string>> {};

template <typename Element, typename Allocator>
struct IsValueConvertible<std::vector<Element, Allocator>>
    : std::bool_constant<std::is_default_constructible_v<Allocator> &&
                         IsValueConvertible<std::remove_cv_t<Element>>::value> {};

template <typename Element, typename Compare, typename Allocator>
struct IsValueConvertible<std::map<std::string, Element, Compare, Allocator>>
    : std::bool_constant<std::is_default_constructible_v<Compare> &&
                         std::is_default_constructible_v<Allocator> &&
                         IsValueConvertible<std::remove_cv_t<Element>>::value> {};

template <typename Element, typename Hash, typename Equal, typename Allocator>
struct IsValueConvertible<std::unordered_map<std::string, Element, Hash, Equal, Allocator>>
    : std::bool_constant<std::is_default_constructible_v<Hash> &&
                         std::is_default_constructible_v<Equal> &&
                         std::is_default_constructible_v<Allocator> &&
                         IsValueConvertible<std::remove_cv_t<Element>>::value> {};

template <typename Integer> [[nodiscard]] bool fitsInInteger(const std::int64_t value) noexcept {
    if constexpr(std::numeric_limits<Integer>::digits >=
                 std::numeric_limits<std::int64_t>::digits) {
        return true;
    } else {
        return value >= static_cast<std::int64_t>(std::numeric_limits<Integer>::lowest()) &&
               value <= static_cast<std::int64_t>(std::numeric_limits<Integer>::max());
    }
}

template <typename T> struct Converter;

template <> struct Converter<bool> {
    [[nodiscard]] static Result<bool> from(const Value& value, const std::string_view path) {
        if(!value.isBoolean()) {
            return Result<bool>::failure(typeMismatch(path, "boolean", value.type()));
        }
        return Result<bool>::success(value.asBoolean());
    }

    [[nodiscard]] static Value to(const bool value) { return Value{value}; }
};

template <typename T>
    requires is_signed_integer<T>
struct Converter<T> {
    [[nodiscard]] static Result<T> from(const Value& value, const std::string_view path) {
        if(!value.isInteger()) {
            return Result<T>::failure(typeMismatch(path, "integer", value.type()));
        }

        const std::int64_t integer = value.asInteger();
        if(!fitsInInteger<T>(integer)) {
            return Result<T>::failure({
                .code = ErrorCode::TypeMismatch,
                .message = "Integer value is outside the target type range",
                .path = std::string{path},
                .details = std::nullopt,
            });
        }
        return Result<T>::success(static_cast<T>(integer));
    }

    [[nodiscard]] static Value to(const T value) { return Value{static_cast<std::int64_t>(value)}; }
};

template <typename T>
    requires std::is_floating_point_v<T>
struct Converter<T> {
    [[nodiscard]] static Result<T> from(const Value& value, const std::string_view path) {
        if(value.isInteger()) {
            return Result<T>::success(static_cast<T>(value.asInteger()));
        }
        if(value.isNumber()) {
            return Result<T>::success(static_cast<T>(value.asNumber()));
        }
        return Result<T>::failure(typeMismatch(path, "number", value.type()));
    }

    [[nodiscard]] static Value to(const T value) { return Value{static_cast<double>(value)}; }
};

template <> struct Converter<std::string> {
    [[nodiscard]] static Result<std::string> from(const Value& value, const std::string_view path) {
        if(!value.isString()) {
            return Result<std::string>::failure(typeMismatch(path, "string", value.type()));
        }
        return Result<std::string>::success(value.asString());
    }

    [[nodiscard]] static Value to(const std::string& value) { return Value{value}; }
};

template <typename Vector> struct VectorConverter {
    using Element = IsVector<Vector>::ElementType;

    [[nodiscard]] static Result<Vector> from(const Value& value, const std::string_view path) {
        if(!value.isArray()) {
            return Result<Vector>::failure(typeMismatch(path, "array", value.type()));
        }

        Vector converted;
        const Value::Array& values = value.asArray();
        converted.reserve(values.size());
        for(std::size_t index = 0; index < values.size(); ++index) {
            auto element = Converter<Element>::from(values[index], appendArrayPath(path, index));
            if(!element) {
                return Result<Vector>::failure(std::move(element.error()));
            }
            converted.push_back(std::move(element.value()));
        }
        return Result<Vector>::success(std::move(converted));
    }

    [[nodiscard]] static Value to(const Vector& value) {
        Value::Array converted;
        converted.reserve(value.size());
        std::transform(value.cbegin(), value.cend(), std::back_inserter(converted),
                       [](const Element& element) { return Converter<Element>::to(element); });
        return Value{std::move(converted)};
    }
};

template <typename Element, typename Allocator>
struct Converter<std::vector<Element, Allocator>>
    : VectorConverter<std::vector<Element, Allocator>> {};

template <typename Map> struct StringMapConverter {
    using Element = IsStringMap<Map>::ElementType;

    [[nodiscard]] static Result<Map> from(const Value& value, const std::string_view path) {
        if(!value.isObject()) {
            return Result<Map>::failure(typeMismatch(path, "object", value.type()));
        }

        Map converted;
        for(const auto& [name, member] : value.asObject()) {
            auto element = Converter<Element>::from(member, appendObjectPath(path, name));
            if(!element) {
                return Result<Map>::failure(std::move(element.error()));
            }
            converted.emplace(name, std::move(element.value()));
        }
        return Result<Map>::success(std::move(converted));
    }

    [[nodiscard]] static Value to(const Map& value) {
        Value::Object converted;
        std::transform(value.cbegin(), value.cend(), std::inserter(converted, converted.end()),
                       [](const auto& entry) {
                           return std::pair<std::string, Value>{
                               entry.first, Converter<Element>::to(entry.second)};
                       });
        return Value{std::move(converted)};
    }
};

template <typename Element, typename Compare, typename Allocator>
struct Converter<std::map<std::string, Element, Compare, Allocator>>
    : StringMapConverter<std::map<std::string, Element, Compare, Allocator>> {};

template <typename Element, typename Hash, typename Equal, typename Allocator>
struct Converter<std::unordered_map<std::string, Element, Hash, Equal, Allocator>>
    : StringMapConverter<std::unordered_map<std::string, Element, Hash, Equal, Allocator>> {};

} // namespace value_converter_detail

/**
 * @brief Reports whether a C++ type can cross the Value conversion boundary.
 *
 * Supported types are bool, signed integer types, floating-point types, std::string,
 * std::vector of a supported element type, and std::map or std::unordered_map with
 * std::string keys and a supported mapped type. Conversion default constructs every
 * container, so allocator, comparator, hash, and key-equality policies must be
 * default constructible; non-default-constructible policies are rejected at compile
 * time instead of failing during adapter instantiation.
 *
 * @tparam T Candidate C++ type, excluding cv/ref qualifiers.
 */
template <typename T>
concept ValueConvertible =
    value_converter_detail::IsValueConvertible<std::remove_cvref_t<T>>::value;

/**
 * @brief Converts a Value into a supported C++ value while retaining its input path.
 *
 * Integer Values may convert to floating-point targets. Integer targets accept only
 * Integer Values and reject values outside the target range. Recursive containers
 * preserve the precise failing array index or object field in their Error path.
 *
 * @tparam T Supported target type.
 * @param value Dynamic input value to convert.
 * @param path Public input location for value.
 * @return Converted C++ value or a TypeMismatch error with the failing input path.
 */
template <ValueConvertible T>
[[nodiscard]] Result<std::remove_cvref_t<T>> fromValue(const Value& value,
                                                       const std::string_view path) {
    using Target = std::remove_cvref_t<T>;
    return value_converter_detail::Converter<Target>::from(value, path);
}

/**
 * @brief Converts a supported C++ value into a Value for an Action result.
 *
 * Container elements are converted recursively. String-key maps are materialized as
 * ordered Value objects, preserving the stable Value object iteration contract.
 *
 * @tparam T Supported source type.
 * @param value C++ value to convert.
 * @return Dynamic representation of value.
 */
template <ValueConvertible T> [[nodiscard]] Value toValue(const T& value) {
    using Source = std::remove_cvref_t<T>;
    return value_converter_detail::Converter<Source>::to(value);
}

} // namespace axiom::detail
