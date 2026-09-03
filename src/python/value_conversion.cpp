#include "value_conversion.hpp"

#include <axiom/foundation/detail/value_path.hpp>
#include <axiom/foundation/value.hpp>

// Python.h must precede pybind11 headers; the SDK umbrella header is required
// even though no symbol is used from it directly.
#include <Python.h>            // IWYU pragma: keep
#include <pybind11/pybind11.h> // IWYU pragma: keep
#include <pybind11/pytypes.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace axiom::python {
namespace {

constexpr std::size_t max_depth = 100;

[[noreturn]] void fail(const std::string& message, const std::string_view path) {
    throw ConversionError{ConversionIssue{.message = message, .path = std::string{path}}};
}

void checkDepth(const std::size_t depth, const std::string_view path) {
    if(depth > max_depth) {
        fail("Maximum value nesting depth exceeded", path);
    }
}

// NOLINTBEGIN(misc-include-cleaner): pybind11 and CPython headers are SYSTEM
// includes, so include-cleaner cannot map their symbol providers.
[[nodiscard]] Value convertInteger(const pybind11::handle& object, const std::string_view path) {
    int overflow = 0;
    const auto converted = PyLong_AsLongLongAndOverflow(object.ptr(), &overflow);
    if(converted == -1 && PyErr_Occurred() != nullptr) {
        throw pybind11::error_already_set();
    }
    if(overflow != 0) {
        fail("Integer value is outside the int64 range", path);
    }
    return Value{static_cast<std::int64_t>(converted)};
}
// NOLINTEND(misc-include-cleaner)

// NOLINTBEGIN(misc-no-recursion): containers mirror the recursive Value shape.
[[nodiscard]] Value
convert(const pybind11::handle& object, std::string_view path, std::size_t depth);
[[nodiscard]] pybind11::object toPython(const Value& value);

// NOLINTBEGIN(misc-include-cleaner): pybind11 and CPython headers are SYSTEM
// includes, so include-cleaner cannot map their symbol providers.
[[nodiscard]] Value
convertList(const pybind11::handle& object, const std::string_view path, const std::size_t depth) {
    checkDepth(depth, path);
    const auto list = pybind11::reinterpret_borrow<pybind11::list>(object);
    Value::Array values;
    values.reserve(list.size());
    std::size_t index = 0;
    for(const pybind11::handle element : list) {
        values.push_back(convert(element, axiom::detail::appendArrayPath(path, index), depth + 1));
        ++index;
    }
    return Value{std::move(values)};
}

[[nodiscard]] Value convertDictionary(const pybind11::handle& object,
                                      const std::string_view path,
                                      const std::size_t depth) {
    checkDepth(depth, path);
    const auto dictionary = pybind11::reinterpret_borrow<pybind11::dict>(object);
    Value::Object values;
    for(const auto [key, member] : dictionary) {
        if(!pybind11::isinstance<pybind11::str>(key)) {
            fail("Object keys must be real strings", path);
        }
        const auto name = pybind11::cast<std::string>(key);
        values.emplace(name,
                       convert(member, axiom::detail::appendObjectPath(path, name), depth + 1));
    }
    return Value{std::move(values)};
}

[[nodiscard]] Value
convert(const pybind11::handle& object, const std::string_view path, const std::size_t depth) {
    if(object.is_none()) {
        return Value{nullptr};
    }
    if(pybind11::isinstance<pybind11::bool_>(object)) {
        return Value{pybind11::cast<bool>(object)};
    }
    if(pybind11::isinstance<pybind11::int_>(object)) {
        return convertInteger(object, path);
    }
    if(pybind11::isinstance<pybind11::float_>(object)) {
        return Value{pybind11::cast<double>(object)};
    }
    if(pybind11::isinstance<pybind11::str>(object)) {
        return Value{pybind11::cast<std::string>(object)};
    }
    if(pybind11::isinstance<pybind11::list>(object)) {
        return convertList(object, path, depth);
    }
    if(pybind11::isinstance<pybind11::dict>(object)) {
        return convertDictionary(object, path, depth);
    }
    fail("Unsupported Python type: " +
             pybind11::cast<std::string>(pybind11::str(pybind11::type::of(object))),
         path);
}
// NOLINTEND(misc-include-cleaner)

[[nodiscard]] pybind11::object toPythonList(const Value::Array& values) {
    pybind11::list list;
    for(const Value& element : values) {
        list.append(toPython(element));
    }
    // C++20 implicitly moves the derived list into the returned base object.
    return list;
}

[[nodiscard]] pybind11::object toPythonObject(const Value::Object& fields) {
    pybind11::dict dictionary;
    for(const auto& [name, member] : fields) {
        dictionary[pybind11::str(name)] = toPython(member);
    }
    // C++20 implicitly moves the derived dict into the returned base object.
    return dictionary;
}

[[nodiscard]] pybind11::object toPython(const Value& value) {
    switch(value.type()) {
    case Value::Type::Null:
        return pybind11::none();
    case Value::Type::Boolean:
        return pybind11::bool_(value.asBoolean());
    case Value::Type::Integer:
        return pybind11::int_(value.asInteger());
    case Value::Type::Number:
        return pybind11::float_(value.asNumber());
    case Value::Type::String:
        return pybind11::str(value.asString());
    case Value::Type::Array:
        return toPythonList(value.asArray());
    case Value::Type::Object:
        return toPythonObject(value.asObject());
    }
    throw std::logic_error{"Unknown Value type"};
}
// NOLINTEND(misc-no-recursion)

} // namespace

Value valueFromPython(const pybind11::handle& object, const std::string_view path) {
    return convert(object, path, 0);
}

pybind11::object valueToPython(const Value& value) { return toPython(value); }

} // namespace axiom::python