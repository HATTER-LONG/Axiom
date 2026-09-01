#pragma once

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace axiom::python {

void bindValue(py::module_& module);
void bindError(py::module_& module);
void bindAction(py::module_& module);
void bindResource(py::module_& module);
void bindIntrospection(py::module_& module);

} // namespace axiom::python