#include <pybind11/cast.h>
#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h> // NOLINT(misc-include-cleaner)

#include "axiom/python/conversion.hpp"
#include "axiom/python/error.hpp"
#include "bindings.hpp"
#include "host_context.hpp"

#include <axiom/action/action_id.hpp>
#include <axiom/action/descriptor.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/action/module.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace py = pybind11;
namespace axiom::python {

namespace {
axiom::ActionId parseActionId(py::handle action) {
    if(py::isinstance<axiom::ActionId>(action)) {
        return action.cast<axiom::ActionId>();
    }
    if(!py::isinstance<py::str>(action)) {
        throw py::type_error("action must be an ActionId or canonical string");
    }
    auto parsed = axiom::ActionId::parse(action.cast<std::string>());
    if(!parsed) {
        unwrapError(parsed.error());
    }
    return parsed.value();
}

void bindActionId(py::module_& module) {
    py::class_<axiom::ActionId>(module, "ActionId")
        .def_static("parse",
                    [](const std::string& text) -> axiom::ActionId {
                        auto result = axiom::ActionId::parse(text);
                        if(!result) {
                            unwrapError(result.error());
                        }
                        return result.value();
                    })
        .def(py::init([](const std::string& text) {
            auto result = axiom::ActionId::parse(text);
            if(!result) {
                unwrapError(result.error());
            }
            return result.value();
        }))
        .def("__str__", [](const axiom::ActionId& id) { return std::string{id.str()}; })
        .def("__repr__",
             [](const axiom::ActionId& id) { return "ActionId('" + std::string{id.str()} + "')"; })
        .def_property_readonly("module",
                               [](const axiom::ActionId& id) { return std::string{id.module()}; })
        .def_property_readonly("action",
                               [](const axiom::ActionId& id) { return std::string{id.action()}; });
}

void bindParameterDescriptor(py::module_& module) {
    py::class_<axiom::ParameterDescriptor>(module, "ParameterDescriptor")
        .def_readonly("name", &axiom::ParameterDescriptor::name)
        .def_readonly("description", &axiom::ParameterDescriptor::description)
        .def_readonly("required", &axiom::ParameterDescriptor::required)
        .def_readonly("type", &axiom::ParameterDescriptor::type)
        .def_property_readonly("default_value",
                               [](const axiom::ParameterDescriptor& pd) -> py::object {
                                   if(pd.default_value.has_value()) {
                                       return fromValue(*pd.default_value);
                                   }
                                   return py::none();
                               })
        .def_property_readonly("has_default_value", [](const axiom::ParameterDescriptor& pd) {
            return pd.default_value.has_value();
        });
}

void bindActionDescriptor(py::module_& module) {
    py::class_<axiom::ActionDescriptor>(module, "ActionDescriptor")
        .def_readonly("id", &axiom::ActionDescriptor::id)
        .def_readonly("description", &axiom::ActionDescriptor::description)
        .def_readonly("parameters", &axiom::ActionDescriptor::parameters)
        .def_readonly("return_type", &axiom::ActionDescriptor::return_type)
        .def_property_readonly("version",
                               [](const axiom::ActionDescriptor& ad) -> py::object {
                                   if(ad.version.has_value()) {
                                       return py::str{*ad.version};
                                   }
                                   return py::none();
                               })
        .def_readonly("tags", &axiom::ActionDescriptor::tags)
        .def_readonly("metadata", &axiom::ActionDescriptor::metadata);
}

void bindModuleDescriptor(py::module_& module) {
    py::class_<axiom::ModuleDescriptor>(module, "ModuleDescriptor")
        .def_readonly("namespace_name", &axiom::ModuleDescriptor::namespace_name)
        .def_readonly("description", &axiom::ModuleDescriptor::description)
        .def_property_readonly("version",
                               [](const axiom::ModuleDescriptor& md) -> py::object {
                                   if(md.version.has_value()) {
                                       return py::str{*md.version};
                                   }
                                   return py::none();
                               })
        .def_readonly("tags", &axiom::ModuleDescriptor::tags)
        .def_readonly("metadata", &axiom::ModuleDescriptor::metadata);
}

void bindInvocationContext(py::module_& module) {
    py::class_<axiom::InvocationContext>(module, "InvocationContext")
        .def(py::init([](const std::string& request_id, const std::string& trace_id,
                         const std::string& caller,
                         const std::map<std::string, std::string, std::less<>>& metadata) {
                 axiom::InvocationContext ctx;
                 ctx.request_id = request_id;
                 ctx.trace_id = trace_id;
                 ctx.caller = caller;
                 ctx.metadata = metadata;
                 return ctx;
             }),
             py::kw_only(), py::arg("request_id") = "", py::arg("trace_id") = "",
             py::arg("caller") = "",
             py::arg("metadata") = std::map<std::string, std::string, std::less<>>{})
        .def_readwrite("request_id", &axiom::InvocationContext::request_id)
        .def_readwrite("trace_id", &axiom::InvocationContext::trace_id)
        .def_readwrite("caller", &axiom::InvocationContext::caller)
        .def_readwrite("metadata", &axiom::InvocationContext::metadata);
}

void bindRuntime(py::module_& module) {
    py::class_<RuntimeWrapper, std::shared_ptr<RuntimeWrapper>>(module, "Runtime")
        .def("modules",
             [](const RuntimeWrapper& self) {
                 auto refs = self.ctx->runtime->discoverModules();
                 std::vector<axiom::ModuleDescriptor> result;
                 result.reserve(refs.size());
                 std::ranges::transform(refs, std::back_inserter(result),
                                        [](const auto& ref) { return ref.get(); });
                 return result;
             })
        .def("actions",
             [](const RuntimeWrapper& self) {
                 auto refs = self.ctx->runtime->discoverActions();
                 std::vector<axiom::ActionDescriptor> result;
                 result.reserve(refs.size());
                 std::ranges::transform(refs, std::back_inserter(result),
                                        [](const auto& ref) { return ref.get(); });
                 return result;
             })
        .def("describe_module",
             [](const RuntimeWrapper& self,
                const std::string& namespace_name) -> axiom::ModuleDescriptor {
                 auto result = self.ctx->runtime->findModule(namespace_name);
                 if(!result) {
                     unwrapError(result.error());
                 }
                 return result.value().get();
             })
        .def(
            "describe_action",
            [](const RuntimeWrapper& self, py::handle action) -> axiom::ActionDescriptor {
                const auto id = parseActionId(action);
                auto result = self.ctx->runtime->findAction(id);
                if(!result) {
                    unwrapError(result.error());
                }
                return result.value().get();
            },
            py::arg("action"))
        .def(
            "invoke",
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
            [](const RuntimeWrapper& self, py::handle action, const py::dict& arguments,
               const py::object& invocation_context) -> py::object {
                const auto id = parseActionId(action);
                axiom::Arguments args;
                for(auto item : arguments) {
                    if(!py::isinstance<py::str>(item.first)) {
                        throw py::type_error("Argument keys must be strings");
                    }
                    auto key = item.first.cast<std::string>();
                    args.emplace(std::move(key),
                                 toValue(py::reinterpret_borrow<py::object>(item.second)));
                }

                axiom::InvocationContext ctx_val;
                if(!invocation_context.is_none()) {
                    ctx_val = invocation_context.cast<axiom::InvocationContext>();
                }

                return unwrap(self.ctx->runtime->invoke(id, args, ctx_val));
            },
            py::arg("action"), py::arg("arguments"), py::arg("context") = py::none());
}
} // namespace

void bindAction(py::module_& module) {
    bindActionId(module);
    bindInvocationContext(module);
    bindModuleDescriptor(module);
    bindActionDescriptor(module);
    bindParameterDescriptor(module);
    bindRuntime(module);
}

} // namespace axiom::python
