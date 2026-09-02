#include "bindings.hpp"
#include "host_context.hpp"

#include <axiom/action/runtime.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_registry.hpp>

#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>

#include <memory>
#include <utility>

namespace py = pybind11;

namespace {

void bindHost(py::module_& module) {
    py::class_<axiom::python::Host, std::shared_ptr<axiom::python::Host>>(module, "Host")
        .def_property_readonly("runtime",
                               [](const axiom::python::Host& self) {
                                   return std::make_shared<axiom::python::RuntimeWrapper>(self.ctx);
                               })
        .def_property_readonly("introspection",
                               [](const axiom::python::Host& self) {
                                   return std::make_shared<axiom::python::IntrospectionWrapper>(
                                       self.ctx);
                               })
        .def("task", [](const axiom::python::Host& self, const axiom::task::TaskId& id) {
            return std::make_shared<axiom::python::TaskWrapper>(self.ctx, id);
        });
}

void bindAttach(py::module_& module) {
    // These holder types are intentionally non-constructible from Python.  They
    // exist solely so an embedding C++ host can pass shared Core objects through
    // _attach() without exposing a Python registration API.
    [[maybe_unused]] auto runtime_holder =
        py::class_<axiom::Runtime, std::shared_ptr<axiom::Runtime>>(module, "_RuntimeHolder");
    [[maybe_unused]] auto resource_registry_holder =
        py::class_<axiom::resource::ResourceRegistry,
                   std::shared_ptr<axiom::resource::ResourceRegistry>>(module,
                                                                       "_ResourceRegistryHolder");
    [[maybe_unused]] auto task_registry_holder =
        py::class_<axiom::task::TaskRegistry, std::shared_ptr<axiom::task::TaskRegistry>>(
            module, "_TaskRegistryHolder");
    module.def("_attach", [](std::shared_ptr<axiom::Runtime> rt,
                             std::shared_ptr<axiom::resource::ResourceRegistry> res,
                             std::shared_ptr<axiom::task::TaskRegistry> tsk) {
        if(!rt || !res || !tsk) {
            throw py::value_error(
                "_attach requires live Runtime, ResourceRegistry and TaskRegistry holders");
        }
        auto ctx = std::make_shared<axiom::python::HostContext>(std::move(rt), std::move(res),
                                                                std::move(tsk));
        return std::make_shared<axiom::python::Host>(std::move(ctx));
    });
}

} // namespace

PYBIND11_MODULE(axiom, module) {
    module.doc() = "Axiom Python binding";

    // Initialization order is fixed: error model, value/type descriptions, action
    // runtime, introspection, task, resource.
    axiom::python::bindError(module);
    axiom::python::bindValue(module);
    axiom::python::bindAction(module);
    axiom::python::bindIntrospection(module);
    axiom::python::bindTaskTypes(module);
    axiom::python::bindResource(module);

    bindAttach(module);
    bindHost(module);
}
