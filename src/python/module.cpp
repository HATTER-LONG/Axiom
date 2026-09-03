#include "error_translation.hpp"
#include "value_conversion.hpp"

#include <axiom/action/invocation_context.hpp>
#include <axiom/foundation/detail/value_path.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/python/host_bridge.hpp>

// Python.h must precede pybind11 headers; the SDK umbrella header is required
// even though no symbol is used from it directly.
#include <Python.h> // IWYU pragma: keep
#include <pybind11/gil.h>
#include <pybind11/pybind11.h> // IWYU pragma: keep
#include <pybind11/pytypes.h>

#include <functional>
#include <map>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace py = pybind11;

namespace axiom::python {
namespace {

// NOLINTBEGIN(misc-include-cleaner): pybind11 and CPython headers are SYSTEM
// includes, so include-cleaner cannot map their symbol providers.
[[nodiscard]] std::string requireString(const py::handle& value, const std::string_view path) {
    if(!py::isinstance<py::str>(value)) {
        throw ConversionError{ConversionIssue{.message = "Context field must be a string",
                                              .path = std::string{path}}};
    }
    return py::cast<std::string>(value);
}
// NOLINTEND(misc-include-cleaner)

[[noreturn]] void unknownContextField(const std::string& name) {
    throw ConversionError{
        ConversionIssue{.message = "Unknown context field: " + name,
                        .path = axiom::detail::appendObjectPath("context", name)}};
}

// NOLINTBEGIN(misc-include-cleaner): pybind11 and CPython headers are SYSTEM
// includes, so include-cleaner cannot map their symbol providers.
[[nodiscard]] std::map<std::string, std::string, std::less<>>
readMetadata(const py::handle& value) {
    if(!py::isinstance<py::dict>(value)) {
        throw ConversionError{ConversionIssue{.message = "Context metadata must be an object",
                                              .path = "context.metadata"}};
    }
    std::map<std::string, std::string, std::less<>> metadata;
    const auto dictionary = py::reinterpret_borrow<py::dict>(value);
    for(const auto [meta_key, meta_value] : dictionary) {
        if(!py::isinstance<py::str>(meta_key)) {
            throw ConversionError{ConversionIssue{.message = "Metadata keys must be strings",
                                                  .path = "context.metadata"}};
        }
        const auto meta_name = py::cast<std::string>(meta_key);
        metadata.emplace(meta_name, requireString(meta_value, axiom::detail::appendObjectPath(
                                                                  "context.metadata", meta_name)));
    }
    return metadata;
}

[[nodiscard]] InvocationContext contextFromPython(const py::object& context) {
    if(context.is_none()) {
        return {};
    }
    if(!py::isinstance<py::dict>(context)) {
        throw py::type_error("context must be a dict or None");
    }
    const auto dictionary = py::cast<py::dict>(context);
    InvocationContext result;
    for(const auto [key, value] : dictionary) {
        if(!py::isinstance<py::str>(key)) {
            throw ConversionError{ConversionIssue{.message = "Context field names must be strings",
                                                  .path = "context"}};
        }
        const auto name = py::cast<std::string>(key);
        if(name == "request_id") {
            result.request_id = requireString(value, "context.request_id");
            continue;
        }
        if(name == "trace_id") {
            result.trace_id = requireString(value, "context.trace_id");
            continue;
        }
        if(name == "caller") {
            result.caller = requireString(value, "context.caller");
            continue;
        }
        if(name == "metadata") {
            result.metadata = readMetadata(value);
            continue;
        }
        unknownContextField(name);
    }
    return result;
}
// NOLINTEND(misc-include-cleaner)

/**
 * @brief Python-facing dispatch capability over one attachable HostHandle.
 *
 * Every call converts Python data into owned C++ values under the GIL, then
 * runs only the Core dispatch with the GIL released, and rebuilds fresh
 * Python objects afterwards.
 */
class Host {
public:
    explicit Host(HostHandle handle) : handle_(std::move(handle)) {}

    // The Python facade contract fixes this keyword signature; params and
    // context are both Python objects by design, so the similar adjacent
    // types cannot be avoided.
    // NOLINTBEGIN(bugprone-easily-swappable-parameters)
    py::object dispatch(std::string method,       /*method*/
                        const py::object& params, /*params*/
                        const py::object& context /*context*/) {
        try {
            if(!py::isinstance<py::dict>(params)) {
                throw py::type_error("params must be a dict");
            }
            const auto params_dict = py::cast<py::dict>(params);
            const InvocationContext invocation_context = contextFromPython(context);
            const Value params_value = valueFromPython(params_dict, {});
            const Result<Value> result = [&] {
                const py::gil_scoped_release release;
                return handle_.dispatch(method, params_value.asObject(), invocation_context);
            }();
            if(!result) {
                if(result.error().code == ErrorCode::HostClosed) {
                    raiseHostClosed(result.error().message);
                }
                raiseAxiomError(result.error());
            }
            return valueToPython(result.value());
        } catch(const py::error_already_set&) {
            throw;
        } catch(const ConversionError& error) {
            raiseConversionError(error);
        } catch(const py::type_error&) {
            throw;
        } catch(const std::bad_alloc&) {
            raiseMemoryError();
        } catch(...) {
            raiseInternalAdapterError();
        }
    }
    // NOLINTEND(bugprone-easily-swappable-parameters)

private:
    HostHandle handle_;
};

[[nodiscard]] Host attach(const HostHandle& handle) {
    if(handle.empty()) {
        raiseHostClosed("Cannot attach an empty host handle");
    }
    if(handle.closed()) {
        raiseHostClosed("Cannot attach a closed host handle");
    }
    return Host{handle};
}

} // namespace
} // namespace axiom::python

// PYBIND11_MODULE is provided by a pybind11 SYSTEM header that
// include-cleaner cannot map.
// NOLINTNEXTLINE(misc-include-cleaner)
PYBIND11_MODULE(_axiom, module) {
    module.doc() = "Axiom application framework Python adapter";
    axiom::python::initializeErrors(module);
    [[maybe_unused]] const auto host_handle_type =
        py::class_<axiom::python::HostHandle>(module, "_HostHandle");
    auto host_type = py::class_<axiom::python::Host>(module, "_Host");
    host_type.def("dispatch", &axiom::python::Host::dispatch, py::arg("method"), py::arg("params"),
                  py::arg("context") = py::none());
    module.def("attach", &axiom::python::attach, py::arg("handle"));
}
