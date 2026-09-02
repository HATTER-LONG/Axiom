#include "axiom/python/conversion.hpp"
#include "axiom/python/error.hpp"
#include "bindings.hpp"
#include "host_context.hpp"

#include <axiom/foundation/error.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_types.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h> // NOLINT(misc-include-cleaner)

#include <memory>
#include <string>
#include <string_view>
#include <utility>

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
    if(!axiom::task::isTerminal(snapshot.state)) {
        return py::none();
    }
    if(!snapshot.value.has_value()) {
        unwrapError({.code = axiom::ErrorCode::TypeMismatch,
                     .message = "Task result cannot be represented as Axiom Value",
                     .path = std::nullopt,
                     .details = std::nullopt});
        return py::none();
    }
    const auto& value_result = *snapshot.value;
    if(!value_result) {
        unwrapError(value_result.error());
        return py::none();
    }
    return fromValue(value_result.value());
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
             [](const axiom::task::TaskId& self, const py::object& other) -> py::object {
                 if(!py::isinstance<axiom::task::TaskId>(other)) {
                     return notImplemented();
                 }
                 return py::bool_{self == other.cast<axiom::task::TaskId>()};
             })
        .def("__lt__",
             [](const axiom::task::TaskId& self, const py::object& other) -> py::object {
                 if(!py::isinstance<axiom::task::TaskId>(other)) {
                     return notImplemented();
                 }
                 return py::bool_{self < other.cast<axiom::task::TaskId>()};
             })
        .def("__le__",
             [](const axiom::task::TaskId& self, const py::object& other) -> py::object {
                 if(!py::isinstance<axiom::task::TaskId>(other)) {
                     return notImplemented();
                 }
                 return py::bool_{self <= other.cast<axiom::task::TaskId>()};
             })
        .def("__gt__",
             [](const axiom::task::TaskId& self, const py::object& other) -> py::object {
                 if(!py::isinstance<axiom::task::TaskId>(other)) {
                     return notImplemented();
                 }
                 return py::bool_{self > other.cast<axiom::task::TaskId>()};
             })
        .def("__ge__",
             [](const axiom::task::TaskId& self, const py::object& other) -> py::object {
                 if(!py::isinstance<axiom::task::TaskId>(other)) {
                     return notImplemented();
                 }
                 return py::bool_{self >= other.cast<axiom::task::TaskId>()};
             })
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
                                       return errorToDict(*td.error);
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

} // namespace

void bindTaskTypes(py::module_& module) {
    bindTaskId(module);
    bindTaskState(module);
    bindProgress(module);
    bindTaskOrigin(module);
    bindTaskDescriptor(module);
    bindTask(module);
}

} // namespace axiom::python
