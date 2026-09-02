#include "axiom/python/error.hpp"
#include "bindings.hpp"
#include "host_context.hpp"

#include <axiom/action/action_id.hpp>
#include <axiom/action/descriptor.hpp>
#include <axiom/introspection/introspection_query.hpp>
#include <axiom/introspection/introspection_service.hpp>
#include <axiom/introspection/runtime_snapshot.hpp>
#include <axiom/resource/resource_descriptor.hpp>
#include <axiom/resource/resource_id.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_types.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // NOLINT(misc-include-cleaner)

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace py = pybind11;
namespace axiom::python {
namespace {

void bindActionQuery(py::module_& module) {
    py::class_<axiom::introspection::ActionQuery>(module, "ActionQuery")
        .def(py::init(
                 [](const std::optional<std::string>& mod, const std::vector<std::string>& tags) {
                     axiom::introspection::ActionQuery q;
                     q.module = mod;
                     q.tags = tags;
                     return q;
                 }),
             py::kw_only(), py::arg("module") = py::none(),
             py::arg("tags") = std::vector<std::string>{})
        .def_readwrite("module", &axiom::introspection::ActionQuery::module)
        .def_readwrite("tags", &axiom::introspection::ActionQuery::tags);
}

void bindResourceQuery(py::module_& module) {
    py::class_<axiom::introspection::ResourceQuery>(module, "ResourceQuery")
        .def(py::init([](const std::optional<std::string>& type) {
                 axiom::introspection::ResourceQuery q;
                 q.type = type;
                 return q;
             }),
             py::kw_only(), py::arg("type") = py::none())
        .def_readwrite("type", &axiom::introspection::ResourceQuery::type);
}

void bindTaskQuery(py::module_& module) {
    py::class_<axiom::introspection::TaskQuery>(module, "TaskQuery")
        .def(py::init([](const std::optional<axiom::task::TaskState>& state,
                         const std::optional<std::string>& origin_action_id,
                         const std::optional<std::string>& origin_request_id) {
                 axiom::introspection::TaskQuery q;
                 q.state = state;
                 q.origin_action_id = origin_action_id;
                 q.origin_request_id = origin_request_id;
                 return q;
             }),
             py::kw_only(), py::arg("state") = py::none(), py::arg("origin_action_id") = py::none(),
             py::arg("origin_request_id") = py::none())
        .def_readwrite("state", &axiom::introspection::TaskQuery::state)
        .def_readwrite("origin_action_id", &axiom::introspection::TaskQuery::origin_action_id)
        .def_readwrite("origin_request_id", &axiom::introspection::TaskQuery::origin_request_id);
}

void bindRuntimeSnapshot(py::module_& module) {
    py::class_<axiom::introspection::RuntimeSnapshot>(module, "RuntimeSnapshot")
        .def_readonly("modules", &axiom::introspection::RuntimeSnapshot::modules)
        .def_readonly("actions", &axiom::introspection::RuntimeSnapshot::actions)
        .def_readonly("resources", &axiom::introspection::RuntimeSnapshot::resources)
        .def_readonly("tasks", &axiom::introspection::RuntimeSnapshot::tasks);
}

void bindIntrospectionService(py::module_& module) {
    py::class_<IntrospectionWrapper, std::shared_ptr<IntrospectionWrapper>>(module,
                                                                            "IntrospectionService")
        .def("modules",
             [](const IntrospectionWrapper& self) { return self.ctx->introspection->modules(); })
        .def(
            "actions",
            [](const IntrospectionWrapper& self,
               const std::optional<axiom::introspection::ActionQuery>& query) {
                if(query.has_value()) {
                    return self.ctx->introspection->actions(*query);
                }
                return self.ctx->introspection->actions();
            },
            py::arg("query") = py::none())
        .def("describe_action",
             [](const IntrospectionWrapper& self,
                const axiom::ActionId& id) -> axiom::ActionDescriptor {
                 auto result = self.ctx->introspection->describeAction(id);
                 if(!result) {
                     unwrapError(result.error());
                 }
                 return result.value();
             })
        .def(
            "resources",
            [](const IntrospectionWrapper& self,
               const std::optional<axiom::introspection::ResourceQuery>& query) {
                if(query.has_value()) {
                    auto result = self.ctx->introspection->resources(*query);
                    if(!result) {
                        unwrapError(result.error());
                    }
                    return result.value();
                }
                return self.ctx->introspection->resources();
            },
            py::arg("query") = py::none())
        .def("describe_resource",
             [](const IntrospectionWrapper& self,
                const axiom::resource::ResourceId& id) -> axiom::resource::ResourceDescriptor {
                 auto result = self.ctx->introspection->describeResource(id);
                 if(!result) {
                     unwrapError(result.error());
                 }
                 return result.value();
             })
        .def(
            "tasks",
            [](const IntrospectionWrapper& self,
               const std::optional<axiom::introspection::TaskQuery>& query) {
                if(query.has_value()) {
                    return self.ctx->introspection->tasks(*query);
                }
                return self.ctx->introspection->tasks();
            },
            py::arg("query") = py::none())
        .def("describe_task",
             [](const IntrospectionWrapper& self,
                const axiom::task::TaskId& id) -> axiom::task::TaskDescriptor {
                 auto result = self.ctx->introspection->describeTask(id);
                 if(!result) {
                     unwrapError(result.error());
                 }
                 return result.value();
             })
        .def("snapshot",
             [](const IntrospectionWrapper& self) { return self.ctx->introspection->snapshot(); });
}
} // namespace

void bindIntrospection(py::module_& module) {
    bindActionQuery(module);
    bindResourceQuery(module);
    bindTaskQuery(module);
    bindRuntimeSnapshot(module);
    bindIntrospectionService(module);
}

} // namespace axiom::python
