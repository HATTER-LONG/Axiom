#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

namespace py = pybind11;

namespace axiom::python {

// Returning Python's NotImplemented lets rich-comparison fall back to the
// reflected operation, so unrelated types compare unequal instead of raising.
inline py::object notImplemented() {
    return py::reinterpret_borrow<py::object>(Py_NotImplemented);
}

void bindError(py::module_& module);
void bindValue(py::module_& module);
void bindAction(py::module_& module);
void bindIntrospection(py::module_& module);
void bindTaskTypes(py::module_& module);
void bindResource(py::module_& module);

} // namespace axiom::python