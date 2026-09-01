#pragma once

#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

namespace py = pybind11;

namespace axiom::python {

void registerAxiomError(py::module_& module);

void registerErrorCode(py::module_& module);

void unwrapError(const axiom::Error& error);

py::object unwrap(axiom::Result<axiom::Value>&& result);

py::object unwrapVoid(axiom::Result<void>&& result);

void throwIfError(const axiom::Result<axiom::Value>& result);

void throwIfError(const axiom::Result<void>& result);

} // namespace axiom::python
