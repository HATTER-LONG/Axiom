#pragma once

/**
 * @file value_path.hpp
 * @brief Shared value-path text and type-mismatch reporting for dynamic boundaries.
 */

#include <axiom/foundation/error.hpp>
#include <axiom/foundation/value.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace axiom::detail {

/**
 * @brief Reports the public name of a Value variant type.
 *
 * @param type Logical Value type to name.
 * @return Stable lowercase type name; a defensive fallback for unknown types.
 */
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

/**
 * @brief Builds a TypeMismatch Error with the received and expected types in details.
 *
 * @param path Public input location where the mismatch occurred.
 * @param expected Stable name of the expected type.
 * @param actual Logical type of the received Value.
 * @return Error carrying a fixed `actual`/`expected` details object.
 */
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
    if(isIdentifierPathSegment(field_name)) {
        if(parent_path.empty()) {
            return std::string{field_name};
        }
        return std::string{parent_path} + '.' + std::string{field_name};
    }
    return std::string{parent_path} + "[\"" + escapePathKey(field_name) + "\"]";
}

} // namespace axiom::detail