#include "axiom/python/error.hpp"
#include "bindings.hpp"

#include <axiom/resource/resource_descriptor.hpp>
#include <axiom/resource/resource_id.hpp>

#include <pybind11/pybind11.h>

#include <string>
#include <string_view>

namespace py = pybind11;
namespace axiom::python {

namespace {

void bindResourceId(py::module_& module) {
    py::class_<axiom::resource::ResourceId>(module, "ResourceId")
        .def_static("parse",
                    [](const std::string& text) -> axiom::resource::ResourceId {
                        auto result = axiom::resource::ResourceId::parse(text);
                        if(!result) {
                            unwrapError(result.error());
                        }
                        return result.value();
                    })
        .def(py::init([](const std::string& text) {
            auto result = axiom::resource::ResourceId::parse(text);
            if(!result) {
                unwrapError(result.error());
            }
            return result.value();
        }))
        .def("__str__", [](const axiom::resource::ResourceId& id) { return std::string{id.str()}; })
        .def("__repr__",
             [](const axiom::resource::ResourceId& id) {
                 return "ResourceId('" + std::string{id.str()} + "')";
             })
        .def("__eq__", [](const axiom::resource::ResourceId& a,
                          const axiom::resource::ResourceId& b) { return a == b; })
        .def("__hash__", [](const axiom::resource::ResourceId& id) {
            return std::hash<std::string_view>{}(id.str());
        });
}

void bindResourceDescriptor(py::module_& module) {
    py::class_<axiom::resource::ResourceDescriptor>(module, "ResourceDescriptor")
        .def_readonly("id", &axiom::resource::ResourceDescriptor::id)
        .def_readonly("type", &axiom::resource::ResourceDescriptor::type);
}

} // namespace

void bindResource(py::module_& module) {
    bindResourceId(module);
    bindResourceDescriptor(module);
}

} // namespace axiom::python
