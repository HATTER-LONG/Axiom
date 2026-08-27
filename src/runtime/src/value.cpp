#include <axiom/runtime/value.hpp>

#include <stdexcept>
#include <utility>

namespace axiom::runtime {

Value::Value() noexcept : m_storage{nullptr} {}

Value::Value(std::nullptr_t) noexcept : m_storage{nullptr} {}

Value::Value(bool value) noexcept : m_storage{value} {}

Value::Value(std::int64_t value) noexcept : m_storage{value} {}

Value::Value(int value) noexcept : m_storage{static_cast<std::int64_t>(value)} {}

Value::Value(double value) noexcept : m_storage{value} {}

Value::Value(std::string value) noexcept : m_storage{std::move(value)} {}

Value::Value(const char* value) : m_storage{std::string{value}} {}

Value::Value(Array value) noexcept : m_storage{std::move(value)} {}

Value::Value(Object value) noexcept : m_storage{std::move(value)} {}

ValueType Value::type() const noexcept {
    switch(m_storage.index()) {
    case 0:
        return ValueType::Null;
    case 1:
        return ValueType::Boolean;
    case 2:
        return ValueType::Integer;
    case 3:
        return ValueType::Number;
    case 4:
        return ValueType::String;
    case 5:
        return ValueType::Array;
    default:
        return ValueType::Object;
    }
}

bool Value::isNull() const noexcept { return std::holds_alternative<std::nullptr_t>(m_storage); }

bool Value::isBoolean() const noexcept { return std::holds_alternative<bool>(m_storage); }

bool Value::isInteger() const noexcept { return std::holds_alternative<std::int64_t>(m_storage); }

bool Value::isNumber() const noexcept {
    return isInteger() || std::holds_alternative<double>(m_storage);
}

bool Value::isString() const noexcept { return std::holds_alternative<std::string>(m_storage); }

bool Value::isArray() const noexcept { return std::holds_alternative<Array>(m_storage); }

bool Value::isObject() const noexcept { return std::holds_alternative<Object>(m_storage); }

bool Value::asBoolean() const {
    if(const auto* value = std::get_if<bool>(&m_storage)) {
        return *value;
    }
    throw std::invalid_argument{"Value is not a Boolean"};
}

std::int64_t Value::asInteger() const {
    if(const auto* value = std::get_if<std::int64_t>(&m_storage)) {
        return *value;
    }
    throw std::invalid_argument{"Value is not an Integer"};
}

double Value::asNumber() const {
    if(const auto* value = std::get_if<std::int64_t>(&m_storage)) {
        return static_cast<double>(*value);
    }
    if(const auto* value = std::get_if<double>(&m_storage)) {
        return *value;
    }
    throw std::invalid_argument{"Value is not a Number"};
}

const std::string& Value::asString() const {
    if(const auto* value = std::get_if<std::string>(&m_storage)) {
        return *value;
    }
    throw std::invalid_argument{"Value is not a String"};
}

const Value::Array& Value::asArray() const {
    if(const auto* value = std::get_if<Array>(&m_storage)) {
        return *value;
    }
    throw std::invalid_argument{"Value is not an Array"};
}

const Value::Object& Value::asObject() const {
    if(const auto* value = std::get_if<Object>(&m_storage)) {
        return *value;
    }
    throw std::invalid_argument{"Value is not an Object"};
}

// NOLINTNEXTLINE(misc-no-recursion): recursive value type compares nested containers by design
bool Value::operator==(const Value& other) const noexcept { return m_storage == other.m_storage; }

bool Value::operator!=(const Value& other) const noexcept { return !(*this == other); }

} // namespace axiom::runtime
