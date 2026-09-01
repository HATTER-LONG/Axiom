#pragma once

#include <axiom/foundation/value.hpp>

#include <pybind11/pybind11.h> // NOLINT(misc-include-cleaner)
#include <pybind11/pytypes.h>

namespace py = pybind11;

namespace axiom::python {

axiom::Value toValue(py::handle obj);

py::object fromValue(const axiom::Value& value);

} // namespace axiom::python
