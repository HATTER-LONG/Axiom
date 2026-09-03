#include "error_translation.hpp"
#include "value_conversion.hpp"

#include <axiom/command/error_code_name.hpp>
#include <axiom/foundation/error.hpp>

// Python.h must precede pybind11 headers; the SDK umbrella header is required
// even though no symbol is used from it directly.
#include <Python.h>            // IWYU pragma: keep
#include <pybind11/pybind11.h> // IWYU pragma: keep
#include <pybind11/pytypes.h>

#include <optional>
#include <string>
#include <string_view>

namespace axiom::python {
namespace {

pybind11::object axiom_error_type;
pybind11::object host_closed_error_type;
pybind11::object conversion_error_type;

void applyErrorAttributes(const pybind11::object& instance, const Error& error) {
    instance.attr("code") = pybind11::str(std::string{command::errorCodeName(error.code)});
    instance.attr("message") = pybind11::str(error.message);
    if(error.path.has_value()) {
        instance.attr("path") = pybind11::str(*error.path);
    } else {
        instance.attr("path") = pybind11::none();
    }
    if(error.details.has_value()) {
        instance.attr("details") = valueToPython(*error.details);
    } else {
        instance.attr("details") = pybind11::none();
    }
}

// NOLINTBEGIN(misc-include-cleaner): pybind11 and CPython headers are SYSTEM
// includes, so include-cleaner cannot map their symbol providers.
[[noreturn]] void setAndRaise(const pybind11::object& type, const pybind11::object& instance) {
    PyErr_SetObject(type.ptr(), instance.ptr());
    throw pybind11::error_already_set();
}
// NOLINTEND(misc-include-cleaner)

} // namespace

// NOLINTBEGIN(misc-include-cleaner): pybind11 and CPython headers are SYSTEM
// includes, so include-cleaner cannot map their symbol providers.
void initializeErrors(pybind11::module_& module) {
    axiom_error_type = pybind11::reinterpret_steal<pybind11::object>(
        PyErr_NewException("_axiom.AxiomError", PyExc_Exception, nullptr));
    host_closed_error_type = pybind11::reinterpret_steal<pybind11::object>(
        PyErr_NewException("_axiom.AxiomHostClosedError", axiom_error_type.ptr(), nullptr));
    conversion_error_type = pybind11::reinterpret_steal<pybind11::object>(
        PyErr_NewException("_axiom.AxiomConversionError", PyExc_ValueError, nullptr));
    module.add_object("AxiomError", axiom_error_type);
    module.add_object("AxiomHostClosedError", host_closed_error_type);
    module.add_object("AxiomConversionError", conversion_error_type);
}
// NOLINTEND(misc-include-cleaner)

void raiseAxiomError(const Error& error) {
    const pybind11::object instance = axiom_error_type(pybind11::str(error.message));
    applyErrorAttributes(instance, error);
    setAndRaise(axiom_error_type, instance);
}

void raiseHostClosed(const std::string_view message) {
    const Error closed{.code = ErrorCode::HostClosed,
                       .message = std::string{message},
                       .path = std::nullopt,
                       .details = std::nullopt};
    const pybind11::object instance = host_closed_error_type(pybind11::str(closed.message));
    applyErrorAttributes(instance, closed);
    setAndRaise(host_closed_error_type, instance);
}

void raiseConversionError(const ConversionError& error) {
    const pybind11::object instance =
        conversion_error_type(pybind11::str(std::string{error.what()}));
    instance.attr("path") = pybind11::str(error.path());
    setAndRaise(conversion_error_type, instance);
}

// NOLINTBEGIN(misc-include-cleaner): pybind11 and CPython headers are SYSTEM
// includes, so include-cleaner cannot map their symbol providers.
void raiseMemoryError() {
    PyErr_SetNone(PyExc_MemoryError);
    throw pybind11::error_already_set();
}

void raiseInternalAdapterError() {
    PyErr_SetString(PyExc_RuntimeError, "Axiom adapter internal failure");
    throw pybind11::error_already_set();
}
// NOLINTEND(misc-include-cleaner)

} // namespace axiom::python
