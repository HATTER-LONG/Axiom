#include "axiom/python/conversion.hpp"
#include "axiom/python/error.hpp"
#include "bindings.hpp"
#include "host_context.hpp"

#include <axiom/action/action_id.hpp>
#include <axiom/action/descriptor.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/introspection/introspection_query.hpp>
#include <axiom/introspection/introspection_service.hpp>
#include <axiom/introspection/runtime_snapshot.hpp>
#include <axiom/resource/resource_descriptor.hpp>
#include <axiom/resource/resource_id.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_types.hpp>

#include <pybind11/cast.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h> // NOLINT(misc-include-cleaner)

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace py = pybind11;
namespace axiom::python {
namespace {
py::object taskResult(const TaskWrapper& self) {
    auto result = self.ctx->tasks->result(self.id);
    if(!result) {
        unwrapError(result.error());
        return py::none();
    }
    const auto& snapshot = result.value();
    if(!snapshot.value.has_value()) {
        auto descriptor = self.ctx->tasks->describe(self.id);
        if(!descriptor) {
            unwrapError(descriptor.error());
        }
        if(snapshot.kind == axiom::task::TaskResultKind::Opaque &&
           axiom::task::isTerminal(descriptor.value().state)) {
            unwrapError({.code = axiom::ErrorCode::TypeMismatch,
                         .message = "Task result cannot be represented as Axiom Value",
                         .path = std::nullopt,
                         .details = std::nullopt});
        }
        return py::none();
    }
    const auto& value_result = *snapshot.value;
    if(!value_result) {
        unwrapError(value_result.error());
        return py::none();
    }
    return fromValue(value_result.value());
}

void bindTask(py::module_& module) {
    py::class_<TaskWrapper, std::shared_ptr<TaskWrapper>>(module, "Task")
        .def_property_readonly("id", [](const TaskWrapper& self) { return self.id; })
        .def("describe",
             [](const TaskWrapper& self) -> axiom::task::TaskDescriptor {
                 auto result = self.ctx->tasks->describe(self.id);
                 if(!result) {
                     unwrapError(result.error());
                 }
                 return result.value();
             })
        .def("state",
             [](const TaskWrapper& self) {
                 auto result = self.ctx->tasks->describe(self.id);
                 if(!result) {
                     unwrapError(result.error());
                 }
                 return result.value().state;
             })
        .def("progress",
             [](const TaskWrapper& self) {
                 auto result = self.ctx->tasks->describe(self.id);
                 if(!result) {
                     unwrapError(result.error());
                 }
                 return result.value().progress;
             })
        .def("cancel",
             [](const TaskWrapper& self) {
                 auto result = self.ctx->tasks->cancel(self.id);
                 if(!result) {
                     unwrapError(result.error());
                 }
             })
        .def("result", &taskResult);
}

void bindTaskId(py::module_& module) {
    py::class_<axiom::task::TaskId>(module, "TaskId")
        .def_static("parse",
                    [](const std::string& text) -> axiom::task::TaskId {
                        auto result = axiom::task::TaskId::parse(text);
                        if(!result) {
                            unwrapError(result.error());
                        }
                        return result.value();
                    })
        .def(py::init([](const std::string& text) {
            auto result = axiom::task::TaskId::parse(text);
            if(!result) {
                unwrapError(result.error());
            }
            return result.value();
        }))
        .def("__str__", [](const axiom::task::TaskId& id) { return std::string{id.str()}; })
        .def(
            "__repr__",
            [](const axiom::task::TaskId& id) { return "TaskId('" + std::string{id.str()} + "')"; })
        .def("__eq__",
             [](const axiom::task::TaskId& a, const axiom::task::TaskId& b) { return a == b; })
        .def("__hash__",
             [](const axiom::task::TaskId& id) { return std::hash<std::string_view>{}(id.str()); });
}

void bindTaskState(py::module_& module) {
    py::enum_<axiom::task::TaskState>(module, "TaskState")
        .value("Pending", axiom::task::TaskState::Pending)
        .value("Running", axiom::task::TaskState::Running)
        .value("Completed", axiom::task::TaskState::Completed)
        .value("Failed", axiom::task::TaskState::Failed)
        .value("Cancelled", axiom::task::TaskState::Cancelled);
}

void bindProgress(py::module_& module) {
    py::class_<axiom::task::Progress>(module, "Progress")
        .def_readonly("value", &axiom::task::Progress::value)
        .def_readonly("message", &axiom::task::Progress::message);
}

void bindTaskOrigin(py::module_& module) {
    py::class_<axiom::task::TaskOrigin>(module, "TaskOrigin")
        .def_readonly("request_id", &axiom::task::TaskOrigin::request_id)
        .def_readonly("trace_id", &axiom::task::TaskOrigin::trace_id)
        .def_readonly("caller", &axiom::task::TaskOrigin::caller)
        .def_readonly("action_id", &axiom::task::TaskOrigin::action_id)
        .def_readonly("metadata", &axiom::task::TaskOrigin::metadata);
}

void bindTaskDescriptor(py::module_& module) {
    py::class_<axiom::task::TaskDescriptor>(module, "TaskDescriptor")
        .def_readonly("id", &axiom::task::TaskDescriptor::id)
        .def_readonly("name", &axiom::task::TaskDescriptor::name)
        .def_readonly("state", &axiom::task::TaskDescriptor::state)
        .def_readonly("progress", &axiom::task::TaskDescriptor::progress)
        .def_property_readonly("error",
                               [](const axiom::task::TaskDescriptor& td) -> py::object {
                                   if(td.error.has_value()) {
                                       py::dict d;
                                       d["code"] = td.error->code;
                                       d["message"] = py::str{td.error->message};
                                       if(td.error->path.has_value()) {
                                           d["path"] = py::str{*td.error->path};
                                       } else {
                                           d["path"] = py::none();
                                       }
                                       if(td.error->details.has_value()) {
                                           d["details"] = fromValue(*td.error->details);
                                       } else {
                                           d["details"] = py::none();
                                       }
                                       return d;
                                   }
                                   return py::none();
                               })
        .def_property_readonly("origin", [](const axiom::task::TaskDescriptor& td) -> py::object {
            if(td.origin.has_value()) {
                return py::cast(*td.origin);
            }
            return py::none();
        });
}

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
    bindTaskId(module);
    bindTaskState(module);
    bindProgress(module);
    bindTaskOrigin(module);
    bindTaskDescriptor(module);
    bindActionQuery(module);
    bindResourceQuery(module);
    bindTaskQuery(module);
    bindRuntimeSnapshot(module);
    bindIntrospectionService(module);
    bindTask(module);
}

} // namespace axiom::python
