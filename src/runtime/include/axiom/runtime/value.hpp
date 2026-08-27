#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace axiom::runtime {

/** @brief Logical types supported by the runtime value model. */
enum class ValueType { Null, Boolean, Integer, Number, String, Array, Object };

/**
 * @brief Dynamic value type used by the runtime ABI.
 *
 * Stores exactly one of the JSON-compatible logical types. Accessors have a
 * precondition on the stored type: calling an accessor with a mismatched type
 * throws std::invalid_argument. These throws are for internal use only; the
 * ABI boundary reports conversion errors through the result/error model.
 */
// NOLINTNEXTLINE(misc-no-recursion): copying a recursive value type is bounded by nesting depth
class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::unordered_map<std::string, Value>;

    /** @brief Constructs a Null value. */
    Value() noexcept;
    /** @brief Constructs a Null value. */
    explicit Value(std::nullptr_t) noexcept;
    /** @brief Constructs a Boolean value. */
    explicit Value(bool value) noexcept;
    /** @brief Constructs an Integer value. */
    explicit Value(std::int64_t value) noexcept;
    /** @brief Constructs an Integer value. */
    explicit Value(int value) noexcept;
    /** @brief Constructs a Number value. */
    explicit Value(double value) noexcept;
    /** @brief Constructs a String value. */
    explicit Value(std::string value) noexcept;
    /** @brief Constructs a String value from a null-terminated string. */
    explicit Value(const char* value);
    /** @brief Constructs an Array value. */
    explicit Value(Array value) noexcept;
    /** @brief Constructs an Object value. */
    explicit Value(Object value) noexcept;

    /** @brief Returns the logical type of the stored value. */
    [[nodiscard]] ValueType type() const noexcept;

    /** @brief Returns whether the stored value is Null. */
    [[nodiscard]] bool isNull() const noexcept;
    /** @brief Returns whether the stored value is Boolean. */
    [[nodiscard]] bool isBoolean() const noexcept;
    /** @brief Returns whether the stored value is Integer. */
    [[nodiscard]] bool isInteger() const noexcept;
    /** @brief Returns whether the stored value is Integer or Number. */
    [[nodiscard]] bool isNumber() const noexcept;
    /** @brief Returns whether the stored value is String. */
    [[nodiscard]] bool isString() const noexcept;
    /** @brief Returns whether the stored value is Array. */
    [[nodiscard]] bool isArray() const noexcept;
    /** @brief Returns whether the stored value is Object. */
    [[nodiscard]] bool isObject() const noexcept;

    /** @brief Returns the stored Boolean; throws std::invalid_argument otherwise. */
    [[nodiscard]] bool asBoolean() const;
    /** @brief Returns the stored Integer; throws std::invalid_argument otherwise. */
    [[nodiscard]] std::int64_t asInteger() const;
    /** @brief Returns the stored Number; also accepts Integer; throws std::invalid_argument
     * otherwise. */
    [[nodiscard]] double asNumber() const;
    /** @brief Returns the stored String; throws std::invalid_argument otherwise. */
    [[nodiscard]] const std::string& asString() const;
    /** @brief Returns the stored Array; throws std::invalid_argument otherwise. */
    [[nodiscard]] const Array& asArray() const;
    /** @brief Returns the stored Object; throws std::invalid_argument otherwise. */
    [[nodiscard]] const Object& asObject() const;

    /** @brief Returns whether both values store the same type and content. */
    [[nodiscard]] bool operator==(const Value& other) const noexcept;
    /** @brief Returns the negation of operator==. */
    [[nodiscard]] bool operator!=(const Value& other) const noexcept;

private:
    using Storage =
        std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object>;

    Storage m_storage;
};

/** @brief Named-parameter protocol: action arguments keyed by parameter name. */
using Arguments = Value::Object;

} // namespace axiom::runtime
