#include "axiom/python/conversion.hpp"
#include "bindings.hpp"

#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>

#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pyerrors.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace py = pybind11;
namespace axiom::python {

namespace {

struct Visit {
    py::handle object;
    bool exiting;

    Visit(py::handle visited_object, bool is_exiting)
        : object(visited_object), exiting(is_exiting) {}
};

bool isContainer(py::handle object) {
    return py::isinstance<py::list>(object) || py::isinstance<py::dict>(object);
}

void pushChildren(py::handle object, std::vector<Visit>& stack) {
    if(py::isinstance<py::list>(object)) {
        const auto list = py::reinterpret_borrow<py::list>(object);
        std::ranges::transform(list, std::back_inserter(stack),
                               [](py::handle item) { return Visit{item, false}; });
        return;
    }

    const auto dict = py::reinterpret_borrow<py::dict>(object);
    std::ranges::transform(dict, std::back_inserter(stack), [](const auto& item) {
        return Visit{py::reinterpret_borrow<py::object>(item.second), false};
    });
}

void checkCycle(py::handle root) {
    // Identity-based cycle detection; Python object addresses are stable while the
    // conversion runs, and pointer comparison avoids rich-compare semantics.
    std::set<const PyObject*> ancestors;
    std::vector<Visit> stack;
    stack.emplace_back(root, false);
    while(!stack.empty()) {
        const auto [object, exiting] = stack.back();
        stack.pop_back();

        if(!isContainer(object)) {
            continue;
        }
        if(exiting) {
            ancestors.erase(object.ptr());
            continue;
        }
        if(!ancestors.insert(object.ptr()).second) {
            throw py::value_error("Circular reference detected in value conversion");
        }

        stack.emplace_back(object, true);
        pushChildren(object, stack);
    }
}

// Value is recursively defined, so conversion intentionally follows the input tree.
// NOLINTBEGIN(misc-no-recursion)
axiom::Value toValueImpl(py::handle obj);

axiom::Value toValueFromList(py::handle obj) {
    const auto list = py::reinterpret_borrow<py::list>(obj);
    axiom::Value::Array arr;
    arr.reserve(list.size());
    std::ranges::transform(list, std::back_inserter(arr),
                           [](py::handle item) { return toValueImpl(item); });
    return axiom::Value{std::move(arr)};
}

axiom::Value toValueFromDict(py::handle obj) {
    const auto dict = py::reinterpret_borrow<py::dict>(obj);
    axiom::Value::Object map;
    for(auto item : dict) {
        if(!py::isinstance<py::str>(item.first)) {
            throw py::type_error("Dict keys must be strings");
        }
        auto key = item.first.cast<std::string>();
        map.emplace(std::move(key), toValueImpl(py::reinterpret_borrow<py::object>(item.second)));
    }
    return axiom::Value{std::move(map)};
}

axiom::Value toValueImpl(py::handle obj) {
    if(obj.is_none()) {
        return axiom::Value{};
    }
    if(py::isinstance<py::bool_>(obj)) {
        return axiom::Value{obj.cast<bool>()};
    }
    if(py::isinstance<py::int_>(obj)) {
        const auto int_obj = py::reinterpret_borrow<py::int_>(obj);
        try {
            const auto val = int_obj.cast<std::int64_t>();
            return axiom::Value{val};
        } catch(const py::cast_error&) {
            PyErr_SetString(PyExc_OverflowError, "Integer value out of int64 range");
            throw py::error_already_set();
        }
    }
    if(py::isinstance<py::float_>(obj)) {
        return axiom::Value{obj.cast<double>()};
    }
    if(py::isinstance<py::str>(obj)) {
        auto s = obj.cast<std::string>();
        return axiom::Value{std::move(s)};
    }
    if(py::isinstance<py::list>(obj)) {
        return toValueFromList(obj);
    }
    if(py::isinstance<py::dict>(obj)) {
        return toValueFromDict(obj);
    }
    throw py::type_error("Unsupported type for Value conversion");
}
// NOLINTEND(misc-no-recursion)

py::object fromValueImpl(const axiom::Value& value);

// Value is recursively defined, so conversion intentionally follows the value tree.
// NOLINTBEGIN(misc-no-recursion)
py::object fromValueArray(const axiom::Value& value) {
    const auto& arr = value.asArray();
    py::list list;
    for(const auto& elem : arr) {
        list.append(fromValueImpl(elem));
    }
    return list;
}

py::object fromValueObject(const axiom::Value& value) {
    const auto& obj = value.asObject();
    py::dict dict;
    for(const auto& [key, val] : obj) {
        dict[key.c_str()] = fromValueImpl(val);
    }
    return dict;
}

py::object fromValueImpl(const axiom::Value& value) {
    switch(value.type()) {
    case axiom::Value::Type::Null:
        return py::none{};
    case axiom::Value::Type::Boolean:
        return py::bool_{value.asBoolean()};
    case axiom::Value::Type::Integer:
        return py::int_{value.asInteger()};
    case axiom::Value::Type::Number:
        return py::float_{value.asNumber()};
    case axiom::Value::Type::String: {
        const std::string& s = value.asString();
        try {
            return py::str{s};
        } catch(const py::error_already_set&) {
            PyErr_SetString(PyExc_UnicodeError, "Invalid UTF-8 in Value string");
            throw py::error_already_set();
        }
    }
    case axiom::Value::Type::Array:
        return fromValueArray(value);
    case axiom::Value::Type::Object:
        return fromValueObject(value);
    }
    throw py::type_error("Unknown Value type");
}
// NOLINTEND(misc-no-recursion)

} // namespace

axiom::Value toValue(py::handle obj) {
    checkCycle(obj);
    return toValueImpl(obj);
}

py::object fromValue(const axiom::Value& value) { return fromValueImpl(value); }

namespace {
void bindTypeDescriptor(py::module_& module) {
    py::class_<axiom::TypeDescriptor> type_desc(module, "TypeDescriptor");

    py::enum_<axiom::TypeDescriptor::Kind>(type_desc, "Kind")
        .value("Null", axiom::TypeDescriptor::Kind::Null)
        .value("Boolean", axiom::TypeDescriptor::Kind::Boolean)
        .value("Integer", axiom::TypeDescriptor::Kind::Integer)
        .value("Number", axiom::TypeDescriptor::Kind::Number)
        .value("String", axiom::TypeDescriptor::Kind::String)
        .value("Array", axiom::TypeDescriptor::Kind::Array)
        .value("Object", axiom::TypeDescriptor::Kind::Object);

    type_desc.def_readonly("kind", &axiom::TypeDescriptor::kind)
        .def_readonly("nullable", &axiom::TypeDescriptor::nullable)
        .def_readonly("description", &axiom::TypeDescriptor::description)
        .def_property_readonly("element_type",
                               [](const axiom::TypeDescriptor& td) -> py::object {
                                   if(td.element_type) {
                                       return py::cast(*td.element_type);
                                   }
                                   return py::none();
                               })
        .def_property_readonly("fields",
                               [](const axiom::TypeDescriptor& td) {
                                   py::dict result;
                                   for(const auto& [key, field] : td.fields) {
                                       if(field) {
                                           result[py::str{key}] = py::cast(*field);
                                       } else {
                                           result[py::str{key}] = py::none();
                                       }
                                   }
                                   return result;
                               })
        .def_property_readonly("value_type", [](const axiom::TypeDescriptor& td) -> py::object {
            if(td.value_type) {
                return py::cast(*td.value_type);
            }
            return py::none();
        });
}
} // namespace

void bindValue(py::module_& module) { bindTypeDescriptor(module); }

} // namespace axiom::python
