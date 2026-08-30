#pragma once

/** @file value_converter.hpp
 * @brief Shared implementation of the typed Action value boundary.
 */

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

[[nodiscard]] std::string appendArrayPath(std::string_view parent_path, std::size_t index);
[[nodiscard]] std::string appendObjectPath(std::string_view parent_path,
                                           std::string_view field_name);

namespace value_converter_detail {

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

[[nodiscard]] inline std::string valueTypeName(const Value::Type type) {
    switch(type) {
    case Value::Type::Null:
        return "null";
    case Value::Type::Boolean:
        return "boolean";
    case Value::Type::Integer:
        return "integer";
    case Value::Type::Number:
        return "number";
    case Value::Type::String:
        return "string";
    case Value::Type::Array:
        return "array";
    case Value::Type::Object:
        return "object";
    }
    return "unknown";
}

[[nodiscard]] inline Error typeMismatch(const std::string_view path,
                                        const std::string_view expected,
                                        const Value::Type actual) {
    return {.code = ErrorCode::TypeMismatch,
            .message =
                "Expected " + std::string{expected} + " but received " + valueTypeName(actual),
            .path = std::string{path},
            .details = Value{Value::Object{{"actual", Value{valueTypeName(actual)}},
                                           {"expected", Value{std::string{expected}}}}}};
}

template <typename Integer> [[nodiscard]] bool fitsInInteger(const std::int64_t value) noexcept {
    if constexpr(std::numeric_limits<Integer>::digits >=
                 std::numeric_limits<std::int64_t>::digits) {
        return true;
    } else {
        return value >= static_cast<std::int64_t>(std::numeric_limits<Integer>::lowest()) &&
               value <= static_cast<std::int64_t>(std::numeric_limits<Integer>::max());
    }
}

/**
 * @brief Tests whether a key can be represented as an unquoted path segment.
 *
 * The accepted ASCII grammar is `[A-Za-z_][A-Za-z0-9_]*`. All other keys use
 * the quoted object-key representation so literal punctuation cannot be confused
 * with path structure.
 *
 * @param key String key to test.
 * @return `true` if key has the unquoted path-segment grammar.
 */
[[nodiscard]] constexpr bool isIdentifierPathSegment(const std::string_view key) noexcept {
    if(key.empty()) {
        return false;
    }
    const auto is_letter = [](const unsigned char character) constexpr {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    };
    const auto is_digit = [](const unsigned char character) constexpr {
        return character >= '0' && character <= '9';
    };
    const auto first = static_cast<unsigned char>(key.front());
    if(!is_letter(first) && first != '_') {
        return false;
    }
    return std::all_of(key.cbegin() + 1, key.cend(), [&](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return is_letter(byte) || is_digit(byte) || byte == '_';
    });
}

/**
 * @brief Maps one byte to its C-style escape sequence.
 *
 * @param byte Raw key byte.
 * @return Escape sequence for backslash, quote, and common control characters; empty
 *         for bytes without a C-style escape.
 */
[[nodiscard]] inline std::string_view commonEscapeSequence(const unsigned char byte) noexcept {
    switch(byte) {
    case '\\':
        return "\\\\";
    case '"':
        return "\\\"";
    case '\b':
        return "\\b";
    case '\f':
        return "\\f";
    case '\n':
        return "\\n";
    case '\r':
        return "\\r";
    case '\t':
        return "\\t";
    default:
        return {};
    }
}

/**
 * @brief Escapes a string key for the quoted object-key path representation.
 *
 * Backslashes, quotes, and common control characters use C-style escapes. Other
 * control bytes and DEL use fixed-width `\\u00XX` escapes. This keeps every quoted
 * key segment unambiguous and reversible without altering UTF-8 bytes.
 *
 * @param key Raw string key to escape.
 * @return Escaped key contents without the surrounding quotes.
 */
[[nodiscard]] inline std::string escapePathKey(const std::string_view key) {
    constexpr std::string_view hexadecimal = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(key.size());
    for(const unsigned char byte : key) {
        const auto escape_sequence = commonEscapeSequence(byte);
        if(!escape_sequence.empty()) {
            escaped += escape_sequence;
            continue;
        }
        if(byte < 0x20U || byte == 0x7FU) {
            escaped += "\\u00";
            escaped += hexadecimal[(byte >> 4U) & 0x0FU];
            escaped += hexadecimal[byte & 0x0FU];
        } else {
            escaped += static_cast<char>(byte);
        }
    }
    return escaped;
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
 * @brief Appends an array index to an input path.
 *
 * @param parent_path Existing input path, or empty for a root array.
 * @param index Zero-based array element index.
 * @return Path in the public array notation, for example `points[2]`.
 */
[[nodiscard]] inline std::string appendArrayPath(const std::string_view parent_path,
                                                 const std::size_t index) {
    return std::string{parent_path} + '[' + std::to_string(index) + ']';
}

/**
 * @brief Appends a named object field to an input path.
 *
 * Identifier keys use the existing dot form (for example, `shape.size`). Other
 * keys use a quoted bracket segment (for example, `shape["a.b"]`). Within a
 * quoted segment, `\\`, `"`, common control characters, and remaining control
 * bytes are escaped so punctuation cannot be mistaken for nesting or an array index.
 * This encoding preserves the established `shape.size.x` and `points[2]` forms.
 *
 * @param parent_path Existing input path, or empty for a root object.
 * @param field_name Object field name.
 * @return An unambiguous object path, for example `shape.size` or `shape["a.b"]`.
 */
[[nodiscard]] inline std::string appendObjectPath(const std::string_view parent_path,
                                                  const std::string_view field_name) {
    if(value_converter_detail::isIdentifierPathSegment(field_name)) {
        if(parent_path.empty()) {
            return std::string{field_name};
        }
        return std::string{parent_path} + '.' + std::string{field_name};
    }
    return std::string{parent_path} + "[\"" + value_converter_detail::escapePathKey(field_name) +
           "\"]";
}

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
