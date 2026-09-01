#include <axiom/action/module.hpp>
#include <axiom/action/module_builder.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/task/task_registry.hpp>

#include <pybind11/cast.h>
#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace py = pybind11;

namespace {
py::object createHost() {
    auto runtime = std::make_shared<axiom::Runtime>();
    auto resources = std::make_shared<axiom::resource::ResourceRegistry>();
    auto tasks = std::make_shared<axiom::task::TaskRegistry>();

    axiom::ModuleBuilder module{axiom::ModuleDescriptor{.namespace_name = "math",
                                                        .description = "Test arithmetic",
                                                        .version = std::nullopt,
                                                        .tags = {"test"},
                                                        .metadata = {}}};
    const auto added = module.add(
        "add", "Adds two integers",
        [](const std::int64_t left, const std::int64_t right) { return left + right; },
        axiom::param("left"), axiom::param("right"));
    if(!added) {
        throw std::runtime_error(added.error().message);
    }
    const auto registered = runtime->registerModule(std::move(module));
    if(!registered) {
        throw std::runtime_error(registered.error().message);
    }

    return py::module_::import("axiom").attr("_attach")(py::cast(runtime), py::cast(resources),
                                                        py::cast(tasks));
}
} // namespace

PYBIND11_MODULE(axiom_python_test_host, module) { module.def("create_host", &createHost); }
