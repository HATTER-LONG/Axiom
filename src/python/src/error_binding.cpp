#include "axiom/python/conversion.hpp"
#include "axiom/python/error.hpp"
#include "bindings.hpp"

#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pyerrors.h>

namespace py = pybind11;
namespace axiom::python {

namespace {
py::object g_axiom_error_type;
}

void registerErrorCode(py::module_& module) {
    py::enum_<axiom::ErrorCode>(module, "ErrorCode")
        .value("InvalidArgument", axiom::ErrorCode::InvalidArgument)
        .value("MissingArgument", axiom::ErrorCode::MissingArgument)
        .value("UnknownArgument", axiom::ErrorCode::UnknownArgument)
        .value("TypeMismatch", axiom::ErrorCode::TypeMismatch)
        .value("NotFound", axiom::ErrorCode::NotFound)
        .value("AlreadyExists", axiom::ErrorCode::AlreadyExists)
        .value("InvalidDescriptor", axiom::ErrorCode::InvalidDescriptor)
        .value("InvocationFailed", axiom::ErrorCode::InvocationFailed)
        .value("InternalError", axiom::ErrorCode::InternalError)
        .value("Cancelled", axiom::ErrorCode::Cancelled);
}

void registerAxiomError(py::module_& module) {
    auto exc_type = py::reinterpret_steal<py::object>(
        PyErr_NewException("axiom.AxiomError", PyExc_Exception, nullptr));
    module.add_object("AxiomError", exc_type);

    g_axiom_error_type = exc_type;
}

void unwrapError(const axiom::Error& error) {
    auto exc_type = g_axiom_error_type;
    auto py_err = exc_type(error.message);

    py_err.attr("code") = error.code;
    py_err.attr("message") = error.message;
    if(error.path.has_value()) {
        py_err.attr("path") = *error.path;
    } else {
        py_err.attr("path") = py::none{};
    }
    if(error.details.has_value()) {
        py_err.attr("details") = fromValue(*error.details);
    } else {
        py_err.attr("details") = py::none{};
    }
    py_err.attr("has_details") = error.details.has_value();

    PyErr_SetObject(exc_type.ptr(), py_err.ptr());
    throw py::error_already_set();
}

py::object unwrap(axiom::Result<axiom::Value>&& result) {
    if(result) {
        return fromValue(result.value());
    }
    unwrapError(result.error());
    return py::none{};
}

py::object unwrapVoid(axiom::Result<void>&& result) {
    if(result) {
        return py::none{};
    }
    unwrapError(result.error());
    return py::none{};
}

void throwIfError(const axiom::Result<axiom::Value>& result) {
    if(!result) {
        unwrapError(result.error());
    }
}

void throwIfError(const axiom::Result<void>& result) {
    if(!result) {
        unwrapError(result.error());
    }
}

void bindError(py::module_& module) {
    registerErrorCode(module);
    registerAxiomError(module);
}

} // namespace axiom::python
