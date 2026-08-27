#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <axiom/runtime/result.hpp>
#include <axiom/runtime/value.hpp>

namespace axiom::runtime::detail {

/** @brief Creates a TypeMismatch error reporting the precise value path. */
inline Error typeMismatchError(std::string_view expected, std::string_view path) {
    return Error{ErrorCode::TypeMismatch, std::string{"Expected "} + std::string{expected},
                 std::string{path}, Value{}};
}

/** @brief Appends an array element index to a value path, e.g. `items[2]`. */
inline std::string elementPath(std::string_view path, std::size_t index) {
    return std::string{path} + "[" + std::to_string(index) + "]";
}

/** @brief Appends an object member key to a value path, e.g. `shape.size`. */
inline std::string memberPath(std::string_view path, const std::string& key) {
    return std::string{path} + "." + key;
}

/**
 * @brief Converts between Value and typed C++ values for action adapters.
 *
 * Left undefined so that unsupported types fail loudly at instantiation.
 * Specializations report conversion failures with a precise Error::m_path:
 * the parameter name for top-level values, dotted keys for object members,
 * and bracketed indices for array elements.
 */
template <typename T> struct ValueConverter {
    static_assert(sizeof(T) == 0, "ValueConverter has no specialization for this type");
};

template <> struct ValueConverter<bool> {
    [[nodiscard]] static Result<bool> fromValue(const Value& value, std::string_view path) {
        if(!value.isBoolean()) {
            return Result<bool>{typeMismatchError("boolean", path)};
        }
        return Result<bool>{value.asBoolean()};
    }

    [[nodiscard]] static Value toValue(bool value) { return Value{value}; }
};

template <> struct ValueConverter<int> {
    [[nodiscard]] static Result<int> fromValue(const Value& value, std::string_view path) {
        if(!value.isInteger()) {
            return Result<int>{typeMismatchError("integer", path)};
        }
        const std::int64_t raw = value.asInteger();
        if(raw < std::numeric_limits<int>::min() || raw > std::numeric_limits<int>::max()) {
            return Result<int>{Error{ErrorCode::TypeMismatch, "Integer out of range for int",
                                     std::string{path}, Value{}}};
        }
        return Result<int>{static_cast<int>(raw)};
    }

    [[nodiscard]] static Value toValue(int value) { return Value{value}; }
};

template <> struct ValueConverter<std::int64_t> {
    [[nodiscard]] static Result<std::int64_t> fromValue(const Value& value, std::string_view path) {
        if(!value.isInteger()) {
            return Result<std::int64_t>{typeMismatchError("integer", path)};
        }
        return Result<std::int64_t>{value.asInteger()};
    }

    [[nodiscard]] static Value toValue(std::int64_t value) { return Value{value}; }
};

template <> struct ValueConverter<double> {
    [[nodiscard]] static Result<double> fromValue(const Value& value, std::string_view path) {
        if(!value.isNumber()) {
            return Result<double>{typeMismatchError("number", path)};
        }
        return Result<double>{value.asNumber()};
    }

    [[nodiscard]] static Value toValue(double value) { return Value{value}; }
};

template <> struct ValueConverter<std::string> {
    [[nodiscard]] static Result<std::string> fromValue(const Value& value, std::string_view path) {
        if(!value.isString()) {
            return Result<std::string>{typeMismatchError("string", path)};
        }
        return Result<std::string>{value.asString()};
    }

    [[nodiscard]] static Value toValue(std::string value) { return Value{std::move(value)}; }
};

template <typename T> struct ValueConverter<std::vector<T>> {
    [[nodiscard]] static Result<std::vector<T>> fromValue(const Value& value,
                                                          std::string_view path) {
        if(!value.isArray()) {
            return Result<std::vector<T>>{typeMismatchError("array", path)};
        }
        const Value::Array& source = value.asArray();
        std::vector<T> elements;
        elements.reserve(source.size());
        for(std::size_t index = 0; index < source.size(); ++index) {
            Result<T> element =
                ValueConverter<T>::fromValue(source[index], elementPath(path, index));
            if(!element.hasValue()) {
                return Result<std::vector<T>>{element.error()};
            }
            elements.push_back(std::move(element).value());
        }
        return Result<std::vector<T>>{std::move(elements)};
    }

    [[nodiscard]] static Value toValue(std::vector<T> value) {
        Value::Array elements;
        elements.reserve(value.size());
        std::ranges::transform(value, std::back_inserter(elements), [](T element) {
            return ValueConverter<T>::toValue(std::move(element));
        });
        return Value{std::move(elements)};
    }
};

template <typename T> struct ValueConverter<std::unordered_map<std::string, T>> {
    using Map = std::unordered_map<std::string, T>;

    [[nodiscard]] static Result<Map> fromValue(const Value& value, std::string_view path) {
        if(!value.isObject()) {
            return Result<Map>{typeMismatchError("object", path)};
        }
        Map members;
        for(const auto& [key, member] : value.asObject()) {
            Result<T> converted = ValueConverter<T>::fromValue(member, memberPath(path, key));
            if(!converted.hasValue()) {
                return Result<Map>{converted.error()};
            }
            members.emplace(key, std::move(converted).value());
        }
        return Result<Map>{std::move(members)};
    }

    [[nodiscard]] static Value toValue(Map value) {
        Value::Object members;
        for(auto& [key, member] : value) {
            members.emplace(key, ValueConverter<T>::toValue(std::move(member)));
        }
        return Value{std::move(members)};
    }
};

} // namespace axiom::runtime::detail
