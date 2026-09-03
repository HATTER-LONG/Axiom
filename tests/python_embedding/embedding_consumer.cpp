#include <axiom/action/invocation_context.hpp>
#include <axiom/action/module_builder.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/async/executor.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/python/host_bridge.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/resource/resource_traits.hpp>
#include <axiom/task/task_registry.hpp>
#include <axiom/task/task_types.hpp>

#include <pybind11/embed.h>
#include <pybind11/eval.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace {

struct Widget final {};

} // namespace

template <> struct axiom::resource::ResourceTraits<::Widget> {
    static constexpr std::string_view type_name = "widget";
};

namespace {

using axiom::ErrorCode;
using axiom::InvocationContext;
using axiom::ModuleBuilder;
using axiom::Result;
using axiom::Runtime;
using axiom::Value;
using axiom::python::HostBridge;
using axiom::python::HostHandle;

struct Probe {
    int invokes{0};
    InvocationContext last;
};

[[nodiscard]] bool registerBasicActions(ModuleBuilder& embed, Probe& probe) {
    return embed.add("ping", "Ping", [] { return 1; }) &&
           embed.add(
               "echoi", "Echo int64", [](const std::int64_t value) { return value; },
               axiom::param("value", "Value")) &&
           embed.add(
               "echob", "Echo bool", [](const bool value) { return value; },
               axiom::param("value", "Value")) &&
           embed.add(
               "echof", "Echo float", [](const double value) { return value; },
               axiom::param("value", "Value")) &&
           embed.add("none", "Returns nothing", [] {}) &&
           embed.add(
               "add", "Add two integers",
               [](const int left, const int right) { return left + right; },
               axiom::param("left", "Left"), axiom::param("right", "Right")) &&
           embed.add(
               "label", "Label", [](const std::string& text) { return text; },
               axiom::ActionOptions{.version = {}, .tags = {"read"}, .metadata = {}},
               axiom::param("text", "Text", Value{"none"})) &&
           embed.addContextual("probe", "Records the diagnostic context",
                               [&probe](const axiom::ActionInvocation& invocation) {
                                   ++probe.invokes;
                                   probe.last = invocation.context();
                                   return 1;
                               });
}

[[nodiscard]] bool registerStructuredValueActions(ModuleBuilder& embed) {
    return embed.add(
               "nested", "Nested",
               [](const std::vector<std::vector<int>>& values) {
                   return static_cast<int>(values.size());
               },
               axiom::param("values", "Nested arrays")) &&
           embed.add(
               "echonested", "Echo nested arrays",
               [](const std::vector<std::vector<int>>& values) { return values; },
               axiom::param("values", "Nested arrays")) &&
           embed.add(
               "echodeep", "Echo deeply nested arrays",
               [](const std::vector<std::vector<std::vector<int>>>& values) { return values; },
               axiom::param("values", "Deep arrays")) &&
           embed.add(
               "fields", "Fields",
               [](const std::map<std::string, int>& fields) {
                   return static_cast<int>(fields.size());
               },
               axiom::param("fields", "String map")) &&
           embed.add(
               "echofields", "Echo string map",
               [](const std::map<std::string, int>& fields) { return fields; },
               axiom::param("fields", "String map")) &&
           embed.add(
               "echomixed", "Echo map of arrays",
               [](const std::map<std::string, std::vector<int>>& fields) { return fields; },
               axiom::param("fields", "Map of arrays"));
}

[[nodiscard]] bool registerFailureActions(ModuleBuilder& embed) {
    return embed.add("fail", "Business failure", []() -> Result<int> {
        return Result<int>::failure({.code = ErrorCode::InvalidArgument,
                                     .message = "shape is invalid",
                                     .path = "shape.size",
                                     .details = std::nullopt});
    }) && embed.add("faildetails", "Business failure with details", []() -> Result<int> {
        return Result<int>::failure(
            {.code = ErrorCode::InvalidArgument,
             .message = "shape is invalid",
             .path = "shape.size",
             .details = Value{Value::Object{{"hint", Value{"retry later"}}}}});
    }) && embed.add("boom", "Throws std::exception", []() -> int {
        throw std::runtime_error{"unexpected"};
    }) && embed.add("explode", "Throws an unknown type", []() -> int { throw 7; });
}

[[nodiscard]] bool registerEmbeddingModule(Runtime& runtime, Probe& probe) {
    ModuleBuilder embed{{.namespace_name = "embed",
                         .description = "Embedding module",
                         .version = "1.0",
                         .tags = {"suite"},
                         .metadata = {{"owner", "tests"}}}};
    if(!registerBasicActions(embed, probe) || !registerStructuredValueActions(embed) ||
       !registerFailureActions(embed)) {
        return false;
    }
    return static_cast<bool>(runtime.registerModule(std::move(embed)));
}

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "axiom_python_embedding: %s\n", message);
    std::exit(2);
}

[[nodiscard]] bool pythonCoverageEnabled() {
#ifdef _MSC_VER
    char* buffer = nullptr;
    std::size_t size = 0;
    if(_dupenv_s(&buffer, &size, "AXIOM_PY_COVERAGE") != 0 || buffer == nullptr) {
        return false;
    }
    const bool enabled = buffer[0] != '\0';
    std::free(buffer);
    return enabled;
#else
    const char* value = std::getenv("AXIOM_PY_COVERAGE");
    return value != nullptr && value[0] != '\0';
#endif
}

struct Fixture {
    Probe probe;
    Runtime runtime;
    axiom::resource::ResourceRegistry resources;
    axiom::task::TaskRegistry tasks;
    std::unique_ptr<axiom::async::Executor> executor;

    Fixture() {
        if(!registerEmbeddingModule(runtime, probe)) {
            fail("module registration failed");
        }
        if(!resources.add(std::make_unique<Widget>())) {
            fail("resource registration failed");
        }
        executor = std::make_unique<axiom::async::Executor>(1);
        const auto submitted =
            tasks.submit(*executor,
                         axiom::task::TaskSubmission{
                             .name = "sequence",
                             .origin = axiom::task::TaskOrigin{.request_id = "req",
                                                               .trace_id = "trace",
                                                               .caller = "suite",
                                                               .action_id = "embed.ping",
                                                               .metadata = {{"k", "v"}}}},
                         [](axiom::task::TaskContext&) { return axiom::Result<int>::success(1); });
        if(!submitted) {
            fail("task submission failed");
        }
        executor->close();
    }
};

struct EmbeddingPaths {
    std::string test_path;
    std::string module_directory;
    std::string facade_directory;
};

// NOLINTBEGIN(misc-include-cleaner): pybind11 and CPython headers are SYSTEM
// includes, so include-cleaner cannot map their symbol providers.
[[nodiscard]] py::dict lastContext(const Fixture& fixture) {
    py::dict metadata;
    for(const auto& [key, value] : fixture.probe.last.metadata) {
        metadata[py::str(key)] = py::str(value);
    }
    py::dict result;
    result["request_id"] = py::str(fixture.probe.last.request_id);
    result["trace_id"] = py::str(fixture.probe.last.trace_id);
    result["caller"] = py::str(fixture.probe.last.caller);
    result["metadata"] = std::move(metadata);
    return result;
}

// Initializes the interpreter from the pinned uv/venv executable. program_name
// and executable point at that interpreter; home is its base_prefix so Windows
// embedding does not treat the host directory as PYTHONHOME when python3XX.dll
// is resolved from PATH. Venv site-packages are added via PYTHONPATH.
void configureInterpreterHome(PyConfig& config) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    const char* python_executable = std::getenv("AXIOM_PYTHON_EXECUTABLE");
    const char* python_home = std::getenv("AXIOM_PYTHON_HOME");
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    if(python_executable == nullptr || python_executable[0] == '\0') {
        return;
    }
    PyStatus status = PyConfig_SetBytesString(&config, &config.program_name, python_executable);
    if(PyStatus_Exception(status) != 0) {
        PyConfig_Clear(&config);
        fail("failed to configure the embedded interpreter program name");
    }
    status = PyConfig_SetBytesString(&config, &config.executable, python_executable);
    if(PyStatus_Exception(status) != 0) {
        PyConfig_Clear(&config);
        fail("failed to configure the embedded interpreter executable");
    }
    if(python_home == nullptr || python_home[0] == '\0') {
        return;
    }
    status = PyConfig_SetBytesString(&config, &config.home, python_home);
    if(PyStatus_Exception(status) != 0) {
        PyConfig_Clear(&config);
        fail("failed to configure the embedded interpreter home");
    }
}
// NOLINTEND(misc-include-cleaner)

// NOLINTBEGIN(misc-include-cleaner): pybind11 and CPython headers are SYSTEM
// includes, so include-cleaner cannot map their symbol providers.
int runTests(const EmbeddingPaths& paths) {
    Fixture fixture;
    HostBridge bridge{fixture.runtime, fixture.resources, fixture.tasks};
    const auto handle = bridge.attach();
    HostBridge closed_bridge{fixture.runtime, fixture.resources, fixture.tasks};
    const auto closed_handle = closed_bridge.attach();
    closed_bridge.close();
    int exit_code = 1;
    {
        PyConfig config;
        PyConfig_InitPythonConfig(&config);
        config.parse_argv = 0;
        configureInterpreterHome(config);
        const py::scoped_interpreter guard{&config};
        try {
            const py::module_ sys = py::module_::import("sys");
            sys.attr("path").attr("insert")(0, py::str(paths.module_directory));
            sys.attr("path").attr("insert")(0, py::str(paths.facade_directory));
            // Build trees expose a top-level _axiom module; installed wheels expose
            // axiom._axiom. Resolve either layout and alias the same module object.
            py::exec("import importlib, sys\n"
                     "try:\n"
                     "    extension = importlib.import_module('_axiom')\n"
                     "except ImportError:\n"
                     "    extension = importlib.import_module('axiom._axiom')\n"
                     "    sys.modules['_axiom'] = extension\n"
                     "sys.modules['axiom._axiom'] = extension\n");
            const py::module_ axiom = py::module_::import("_axiom");
            py::object host = axiom.attr("attach")(py::cast(handle));
            py::object support =
                py::module_::import("types").attr("ModuleType")(py::str("axiom_test_host"));
            support.attr("host") = std::move(host);
            support.attr("close") = py::cpp_function([&bridge] { bridge.close(); });
            support.attr("last_context") =
                py::cpp_function([&fixture] { return lastContext(fixture); });
            support.attr("closed_handle") =
                py::cpp_function([&closed_handle] { return py::cast(closed_handle); });
            support.attr("empty_handle") = py::cpp_function([] { return py::cast(HostHandle{}); });
            support.attr("raw_handle") = py::cpp_function([&handle] { return py::cast(handle); });
            sys.attr("modules")[py::str("axiom_test_host")] = support;
            const py::module_ pytest = py::module_::import("pytest");
            py::list arguments;
            arguments.append(py::str(paths.test_path));
            arguments.append("-q");
            arguments.append("-p");
            arguments.append("no:cacheprovider");
            if(pythonCoverageEnabled()) {
                arguments.append("--cov=axiom");
                arguments.append("--cov-report=term-missing");
                arguments.append("--cov-fail-under=90");
            }
            const py::object returned = pytest.attr("main")(arguments);
            exit_code = py::cast<int>(returned);
            sys.attr("modules")[py::str("axiom_test_host")] = py::none();
        } catch(const py::error_already_set& error) {
            std::fprintf(stderr, "embedded interpreter failure: %s\n", error.what());
            exit_code = 1;
        }
    }
    bridge.close();
    return exit_code;
}
// NOLINTEND(misc-include-cleaner)

} // namespace

int main(int argc, char** argv) {
    if(argc != 4) {
        std::fprintf(stderr, "usage: axiom_python_embedding <test-file> <module-directory> "
                             "<facade-directory>\n");
        return 2;
    }
    return runTests(EmbeddingPaths{
        .test_path = argv[1], .module_directory = argv[2], .facade_directory = argv[3]});
}