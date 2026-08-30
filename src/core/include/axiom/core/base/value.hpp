#pragma once

#include <axiom/core/export.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace axiom::core {

/**
 * @brief Reports an attempt to read a Value through an incompatible type accessor.
 *
 * Value access failures are local programming errors. They are intentionally distinct
 * from Error, which represents expected failures at a public invocation boundary.
 */
class AXIOM_CORE_API ValueTypeError final : public std::logic_error {
public:
    /**
     * @brief Creates a type-access error.
     *
     * @param message Description of the incompatible requested and actual types.
     */
    explicit ValueTypeError(const char* message) : std::logic_error(message) {}
};

/**
 * @brief A JSON-shaped dynamic value used at Core boundaries.
 *
 * Objects retain lexicographic key iteration order so callers can produce stable
 * discovery, diagnostics, and future serialized representations.
 *
 * @note Moving a Value transfers its payload and leaves the source as Null. Self-move
 * assignment leaves the Value unchanged.
 */
class AXIOM_CORE_API Value {
public:
    /** @brief The logical type currently held by a Value. */
    enum class Type : std::uint8_t { Null, Boolean, Integer, Number, String, Array, Object };

    /** @brief Ordered sequence of dynamic values. */
    using Array = std::vector<Value>;
    /** @brief String-keyed dynamic values with stable iteration order. */
    using Object = std::map<std::string, Value, std::less<>>;

    /** @brief Creates a null value. */
    Value() noexcept = default;
    /** @brief Copies a dynamic value and its immutable payload. */
    Value(const Value&) = default;
    /**
     * @brief Transfers a dynamic value and resets the source to Null.
     *
     * @param other Value whose payload is transferred.
     * @post `other.isNull()` is true.
     */
    Value(Value&& other) noexcept : storage_(std::move(other.storage_)) {
        other.storage_ = nullptr;
    }
    /** @brief Copies a dynamic value and its immutable payload. */
    Value& operator=(const Value&) = default;
    /**
     * @brief Transfers a dynamic value and resets the source to Null.
     *
     * Self-move assignment preserves this Value.
     *
     * @param other Value whose payload is transferred.
     * @return This Value.
     * @post `other.isNull()` is true unless `this == &other`.
     */
    Value& operator=(Value&& other) noexcept {
        if(this != &other) {
            storage_ = std::move(other.storage_);
            other.storage_ = nullptr;
        }
        return *this;
    }
    /**
     * @brief Creates a null value.
     * @param value Null marker.
     */
    Value(std::nullptr_t value) noexcept : storage_(value) {}
    /**
     * @brief Creates a boolean value.
     * @param value Boolean payload.
     */
    explicit Value(bool value) noexcept : storage_(value) {}
    /**
     * @brief Creates an integer value.
     * @param value Signed 64-bit payload.
     */
    explicit Value(std::int64_t value) noexcept : storage_(value) {}
    /**
     * @brief Creates an integer value from another signed integral type.
     * @tparam Integer Signed integral type narrower than std::int64_t.
     * @param value Integral payload.
     */
    template <typename Integer>
        requires(std::is_integral_v<Integer> && std::is_signed_v<Integer> &&
                 !std::is_same_v<std::remove_cv_t<Integer>, bool> &&
                 !std::is_same_v<std::remove_cv_t<Integer>, std::int64_t>)
    explicit Value(Integer value) noexcept : storage_(static_cast<std::int64_t>(value)) {}
    /**
     * @brief Creates a number value.
     * @param value Double-precision payload.
     */
    explicit Value(double value) noexcept : storage_(value) {}
    /**
     * @brief Creates a string value by taking ownership of text.
     * @param value UTF-8-compatible text.
     */
    explicit Value(std::string value) : storage_(std::move(value)) {}
    /**
     * @brief Creates a string value from a null-terminated string.
     * @param value Null-terminated text. Must not be null.
     */
    explicit Value(const char* value) : storage_(std::string{value}) {}
    /**
     * @brief Creates an array value by taking ownership of its elements.
     * @param value Array payload.
     */
    explicit Value(Array value) : storage_(std::make_shared<const Array>(std::move(value))) {}
    /**
     * @brief Creates an object value by taking ownership of its fields.
     * @param value Ordered object payload.
     */
    explicit Value(Object value) : storage_(std::make_shared<const Object>(std::move(value))) {}

    /** @brief Returns the logical type of this value. */
    [[nodiscard]] Type type() const noexcept;
    /** @brief Returns whether this is a null value. */
    [[nodiscard]] bool isNull() const noexcept;
    /** @brief Returns whether this is a boolean value. */
    [[nodiscard]] bool isBoolean() const noexcept;
    /** @brief Returns whether this is an integer value. */
    [[nodiscard]] bool isInteger() const noexcept;
    /** @brief Returns whether this is a number value. */
    [[nodiscard]] bool isNumber() const noexcept;
    /** @brief Returns whether this is a string value. */
    [[nodiscard]] bool isString() const noexcept;
    /** @brief Returns whether this is an array value. */
    [[nodiscard]] bool isArray() const noexcept;
    /** @brief Returns whether this is an object value. */
    [[nodiscard]] bool isObject() const noexcept;

    /**
     * @brief Returns the boolean payload.
     * @return Boolean payload.
     * @throws ValueTypeError If this is not a boolean.
     */
    [[nodiscard]] bool asBoolean() const;
    /**
     * @brief Returns the integer payload.
     * @return Signed 64-bit payload.
     * @throws ValueTypeError If this is not an integer.
     */
    [[nodiscard]] std::int64_t asInteger() const;
    /**
     * @brief Returns the number payload.
     * @return Double-precision payload.
     * @throws ValueTypeError If this is not a number.
     */
    [[nodiscard]] double asNumber() const;
    /**
     * @brief Returns the string payload.
     * @return Reference valid until this Value is modified or destroyed.
     * @throws ValueTypeError If this is not a string.
     */
    [[nodiscard]] const std::string& asString() const;
    /**
     * @brief Returns the array payload.
     * @return Reference valid until this Value is modified or destroyed.
     * @throws ValueTypeError If this is not an array.
     */
    [[nodiscard]] const Array& asArray() const;
    /**
     * @brief Returns the object payload.
     * @return Reference valid until this Value is modified or destroyed.
     * @throws ValueTypeError If this is not an object.
     */
    [[nodiscard]] const Object& asObject() const;

private:
    using Storage = std::variant<std::nullptr_t,
                                 bool,
                                 std::int64_t,
                                 double,
                                 std::string,
                                 std::shared_ptr<const Array>,
                                 std::shared_ptr<const Object>>;

    Storage storage_{nullptr};
};

/** @brief Named invocation parameters represented as an ordered Value object. */
using Arguments = Value::Object;

inline Value::Type Value::type() const noexcept { return static_cast<Type>(storage_.index()); }

inline bool Value::isNull() const noexcept { return type() == Type::Null; }
inline bool Value::isBoolean() const noexcept { return type() == Type::Boolean; }
inline bool Value::isInteger() const noexcept { return type() == Type::Integer; }
inline bool Value::isNumber() const noexcept { return type() == Type::Number; }
inline bool Value::isString() const noexcept { return type() == Type::String; }
inline bool Value::isArray() const noexcept { return type() == Type::Array; }
inline bool Value::isObject() const noexcept { return type() == Type::Object; }

inline bool Value::asBoolean() const {
    if(!isBoolean()) {
        throw ValueTypeError{"Value is not a boolean"};
    }
    return std::get<bool>(storage_);
}

inline std::int64_t Value::asInteger() const {
    if(!isInteger()) {
        throw ValueTypeError{"Value is not an integer"};
    }
    return std::get<std::int64_t>(storage_);
}

inline double Value::asNumber() const {
    if(!isNumber()) {
        throw ValueTypeError{"Value is not a number"};
    }
    return std::get<double>(storage_);
}

inline const std::string& Value::asString() const {
    if(!isString()) {
        throw ValueTypeError{"Value is not a string"};
    }
    return std::get<std::string>(storage_);
}

inline const Value::Array& Value::asArray() const {
    if(!isArray()) {
        throw ValueTypeError{"Value is not an array"};
    }
    return *std::get<std::shared_ptr<const Array>>(storage_);
}

inline const Value::Object& Value::asObject() const {
    if(!isObject()) {
        throw ValueTypeError{"Value is not an object"};
    }
    return *std::get<std::shared_ptr<const Object>>(storage_);
}

} // namespace axiom::core
