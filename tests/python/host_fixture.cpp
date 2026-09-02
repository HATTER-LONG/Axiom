// Test-only embedding host for the axiom Python surface.  Builds a populated
// Runtime/ResourceRegistry/TaskRegistry and injects them through axiom._attach(),
// exposing a few deterministic controls that the Python tests drive.

#include <axiom/action/action_id.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/action/module.hpp>
#include <axiom/action/module_builder.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/async/executor.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/resource/resource_traits.hpp>
#include <axiom/task/task_registry.hpp>
#include <axiom/task/task_types.hpp>

#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>

#include <condition_variable>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace {

struct Widget final {};
struct Geometry final {};

} // namespace

namespace axiom::resource {
template <> struct ResourceTraits<::Widget> {
    static constexpr std::string_view type_name = "widget";
};
template <> struct ResourceTraits<::Geometry> {
    static constexpr std::string_view type_name = "geometry";
};
} // namespace axiom::resource

namespace {

class Gate final {
public:
    void release() {
        {
            std::scoped_lock const lock{mutex_};
            released_ = true;
        }
        cv_.notify_all();
    }

    void wait() {
        std::unique_lock lock{mutex_};
        cv_.wait(lock, [this] { return released_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool released_{false};
};

template <typename T> T expect(axiom::Result<T>&& result) {
    if(!result) {
        throw std::runtime_error(result.error().message);
    }
    return std::move(result).value();
}

void registerMathModule(axiom::Runtime& runtime, const std::shared_ptr<Gate>& invoke_gate) {
    axiom::ModuleBuilder module{axiom::ModuleDescriptor{.namespace_name = "math",
                                                        .description = "Test arithmetic and echo",
                                                        .version = std::nullopt,
                                                        .tags = {"test"},
                                                        .metadata = {}}};
    auto add = module.add(
        "add", "Adds two integers",
        [](const std::int64_t left, const std::int64_t right) { return left + right; },
        axiom::param("left"), axiom::param("right"));
    if(!add) {
        throw std::runtime_error(add.error().message);
    }
    expect<void>(module.add("echo_string", "Echoes a string",
                            [](const std::string& text) { return text; },
                            axiom::param("text")));
    expect<void>(module.add("echo_bool", "Echoes a boolean", [](const bool flag) { return flag; },
                            axiom::param("flag")));
    expect<void>(module.add("echo_number", "Echoes a number",
                            [](const double x) { return x; }, axiom::param("x")));
    expect<void>(module.add("echo_list", "Echoes an integer list",
                            [](const std::vector<std::int64_t>& values) { return values; },
                            axiom::param("values")));
    expect<void>(module.add(
        "echo_map", "Echoes a string map",
        [](const std::map<std::string, std::string, std::less<>>& table) { return table; },
        axiom::param("table")));
    expect<void>(module.add(
        "echo_nested", "Echoes nested containers",
        [](const std::map<std::string, std::vector<std::int64_t>, std::less<>>& table) {
            return table;
        },
        axiom::param("table")));
    expect<void>(module.add("bad_utf8", "Returns an invalid UTF-8 string", [] {
        return std::string{"\xff\xfe invalid"};
    }));
    expect<void>(module.add("fail_details", "Returns a structured failure",
                            []() -> axiom::Result<std::int64_t> {
                                return axiom::Result<std::int64_t>::failure(
                                    {.code = axiom::ErrorCode::InvocationFailed,
                                     .message = "structured failure",
                                     .path = std::string{"shape.size.x"},
                                     .details = axiom::Value{axiom::Value::Object{
                                         {"actual", axiom::Value{std::string{"string"}}},
                                         {"expected", axiom::Value{std::string{"integer"}}}}}});
                            }));
    expect<void>(module.add("fail_null_details", "Returns a failure whose details are Null",
                            []() -> axiom::Result<std::int64_t> {
                                return axiom::Result<std::int64_t>::failure(
                                    {.code = axiom::ErrorCode::InvalidArgument,
                                     .message = "null details",
                                     .path = std::nullopt,
                                     .details = axiom::Value{}});
                            }));
    expect<void>(module.add("fail_plain", "Returns a bare failure",
                            []() -> axiom::Result<std::int64_t> {
                                return axiom::Result<std::int64_t>::failure(
                                    {.code = axiom::ErrorCode::AlreadyExists,
                                     .message = "already registered",
                                     .path = std::nullopt,
                                     .details = std::nullopt});
                            }));
    expect<void>(module.add("throw_runtime", "Throws a runtime error", []() -> std::int64_t {
        throw std::runtime_error("boom");
    }));
    expect<void>(module.add("throw_unknown", "Throws a non-standard exception", []() -> std::int64_t {
        throw 7;
    }));
    expect<void>(module.add("wait_gate", "Blocks until the host releases the invoke gate",
                            [gate = invoke_gate]() -> std::string {
                                gate->wait();
                                return "done";
                            }));
    expect<void>(module.addContextual(
        "context_echo", "Echoes the invocation context",
        [](const axiom::ActionInvocation& invocation) {
            std::map<std::string, std::string, std::less<>> echoed;
            const auto& context = invocation.context();
            echoed["action_id"] = std::string{invocation.actionId().str()};
            echoed["request_id"] = context.request_id;
            echoed["trace_id"] = context.trace_id;
            echoed["caller"] = context.caller;
            for(const auto& [key, value] : context.metadata) {
                echoed.emplace("meta." + key, value);
            }
            return echoed;
        }));
    const auto registered = runtime.registerModule(std::move(module));
    if(!registered) {
        throw std::runtime_error(registered.error().message);
    }
}

void registerMeshModule(axiom::Runtime& runtime) {
    axiom::ModuleBuilder module{axiom::ModuleDescriptor{.namespace_name = "mesh",
                                                        .description = "Test mesh actions",
                                                        .version = std::string{"1.0.0"},
                                                        .tags = {"geometry"},
                                                        .metadata = {}}};
    expect<void>(module.add(
        "rebuild", "Rebuilds the mesh",
        [](const double quality) {
            std::map<std::string, std::vector<std::int64_t>, std::less<>> summary;
            summary["points"] = {static_cast<std::int64_t>(quality * 10.0), 2, 3};
            return summary;
        },
        axiom::ActionOptions{.version = std::string{"1.2.0"},
                             .tags = {"geometry", "write"},
                             .metadata = {{"author", "test"}}},
        axiom::param("quality", "Rebuild quality factor", axiom::Value{0.5})));
    expect<void>(module.add(
        "inspect", "Inspects the mesh", []() -> std::int64_t { return 1; },
        axiom::ActionOptions{.version = std::nullopt, .tags = {"geometry", "read"}, .metadata = {}}));
    const auto registered = runtime.registerModule(std::move(module));
    if(!registered) {
        throw std::runtime_error(registered.error().message);
    }
}

struct FixtureState final {
    FixtureState();

    std::shared_ptr<axiom::Runtime> runtime;
    std::shared_ptr<axiom::resource::ResourceRegistry> resources;
    std::shared_ptr<axiom::task::TaskRegistry> tasks;

    std::shared_ptr<Gate> invoke_gate = std::make_shared<Gate>();
    std::shared_ptr<Gate> task_gate = std::make_shared<Gate>();
    std::shared_ptr<Gate> blocked_gate = std::make_shared<Gate>();
    axiom::async::Executor live{4};
    axiom::async::Executor blocked{1};

    std::vector<std::shared_future<void>> gated_settled;
    std::vector<std::thread> helpers;

    ~FixtureState() {
        task_gate->release();
        invoke_gate->release();
        blocked_gate->release();
        for(auto& helper : helpers) {
            if(helper.joinable()) {
                helper.join();
            }
        }
    }
};

FixtureState::FixtureState() {
    runtime = std::make_shared<axiom::Runtime>();
    resources = std::make_shared<axiom::resource::ResourceRegistry>();
    tasks = std::make_shared<axiom::task::TaskRegistry>();

    registerMathModule(*runtime, invoke_gate);
    registerMeshModule(*runtime);

    expect(resources->add(std::make_unique<Widget>()));
    expect(resources->add(std::make_unique<Widget>()));
    expect(resources->add(std::make_unique<Geometry>()));

    static_cast<void>(blocked.submit([gate = blocked_gate] { gate->wait(); }));
    expect(tasks->submit(
        blocked, axiom::task::TaskSubmission{.name = "pending-blocked", .origin = std::nullopt},
        [](const axiom::task::TaskContext&) { return axiom::Result<axiom::Value>::success(axiom::Value{}); }));
    const auto cancelled_before_run = expect(tasks->submit(
        blocked,
        axiom::task::TaskSubmission{.name = "cancelled-before-run", .origin = std::nullopt},
        [](const axiom::task::TaskContext&) { return axiom::Result<axiom::Value>::success(axiom::Value{}); }));
    expect<void>(tasks->cancel(cancelled_before_run.id()));

    std::vector<std::future<void>> immediate;
    auto track = [&immediate](auto&& body) {
        auto done = std::make_shared<std::promise<void>>();
        immediate.push_back(done->get_future());
        return [done, body = std::forward<decltype(body)>(body)](
                   axiom::task::TaskContext& context) mutable {
            auto result = body(context);
            done->set_value();
            return result;
        };
    };

    expect(tasks->submit(
        live,
        axiom::task::TaskSubmission{
            .name = "completed-value",
            .origin = axiom::task::TaskOrigin{.request_id = "req-77",
                                               .trace_id = "trace-77",
                                               .caller = "python-test",
                                               .action_id = "math.add",
                                               .metadata = {{"origin", "fixture"}}}},
        track([](const axiom::task::TaskContext&) {
            return axiom::Result<axiom::Value>::success(axiom::Value{42});
        })));
    expect(tasks->submit(
        live,
        axiom::task::TaskSubmission{.name = "completed-void", .origin = std::nullopt},
        track([](const axiom::task::TaskContext&) { return axiom::Result<void>::success(); })));
    expect(tasks->submit(
        live,
        axiom::task::TaskSubmission{.name = "failed-task", .origin = std::nullopt},
        track([](const axiom::task::TaskContext&) {
            return axiom::Result<axiom::Value>::failure(
                {.code = axiom::ErrorCode::InvalidArgument,
                 .message = "bad input",
                 .path = std::string{"input.x"},
                 .details = axiom::Value{axiom::Value::Object{
                     {"reason", axiom::Value{std::string{"overflow"}}}}}});
        })));
    expect(tasks->submit(
        live,
        axiom::task::TaskSubmission{.name = "opaque-done", .origin = std::nullopt},
        track([](const axiom::task::TaskContext&) { return axiom::Result<int>::success(3); })));
    expect(tasks->submit(
        live,
        axiom::task::TaskSubmission{.name = "removable-value", .origin = std::nullopt},
        track([](const axiom::task::TaskContext&) {
            return axiom::Result<axiom::Value>::success(axiom::Value{std::string{"removable"}});
        })));
    for(auto& pending : immediate) {
        pending.wait();
    }

    auto track_gated = [this](auto&& body) {
        auto settled = std::make_shared<std::promise<void>>();
        gated_settled.push_back(settled->get_future().share());
        auto entered = std::make_shared<std::promise<void>>();
        auto entered_future = entered->get_future();
        auto wrapped = [settled, entered,
                        body = std::forward<decltype(body)>(body)](
                           axiom::task::TaskContext& context) mutable {
            entered->set_value();
            auto result = body(context);
            settled->set_value();
            return result;
        };
        return std::make_pair(std::move(wrapped), std::move(entered_future));
    };

    auto submit_gated = [this, &track_gated](std::string name,
                                             std::optional<axiom::task::TaskOrigin> origin,
                                             auto&& body) {
        auto [wrapped, entered] = track_gated(std::forward<decltype(body)>(body));
        expect(tasks->submit(live,
                             axiom::task::TaskSubmission{.name = std::move(name),
                                                         .origin = std::move(origin)},
                             std::move(wrapped)));
        entered.wait();
    };

    std::promise<void> progress_reported;
    auto progress_future = progress_reported.get_future();
    submit_gated(
        "running-gate",
        axiom::task::TaskOrigin{.request_id = "req-gate",
                                .trace_id = "",
                                .caller = "",
                                .action_id = "mesh.rebuild",
                                .metadata = {}},
        [this, &progress_reported](axiom::task::TaskContext& context) {
            context.reportProgress(0.5, "half");
            progress_reported.set_value();
            task_gate->wait();
            return axiom::Result<axiom::Value>::success(axiom::Value{7});
        });
    submit_gated("cancel-running", std::nullopt, [this](axiom::task::TaskContext& context) {
        task_gate->wait();
        if(context.cancellation().requested()) {
            return axiom::Result<axiom::Value>::failure(
                {.code = axiom::ErrorCode::Cancelled,
                 .message = "stopped by caller",
                 .path = std::nullopt,
                 .details = std::nullopt});
        }
        return axiom::Result<axiom::Value>::success(axiom::Value{0});
    });
    submit_gated("opaque-gate", std::nullopt, [this](const axiom::task::TaskContext&) {
        task_gate->wait();
        return axiom::Result<int>::success(9);
    });
    submit_gated("void-gate", std::nullopt, [this](const axiom::task::TaskContext&) {
        task_gate->wait();
        return axiom::Result<void>::success();
    });
    progress_future.wait();
}

std::unique_ptr<FixtureState>& fixtureState() {
    static std::unique_ptr<FixtureState> state;
    return state;
}

py::object createHost() {
    fixtureState() = std::make_unique<FixtureState>();
    auto& state = *fixtureState();
    return py::module_::import("axiom").attr("_attach")(py::cast(state.runtime),
                                                        py::cast(state.resources),
                                                        py::cast(state.tasks));
}

void releaseTaskGate() {
    if(!fixtureState()) {
        throw std::runtime_error("Fixture host has not been created");
    }
    fixtureState()->task_gate->release();
}

void scheduleInvokeGateRelease() {
    if(!fixtureState()) {
        throw std::runtime_error("Fixture host has not been created");
    }
    fixtureState()->helpers.emplace_back(
        [gate = fixtureState()->invoke_gate] { gate->release(); });
}

void waitTasksSettled() {
    if(!fixtureState()) {
        throw std::runtime_error("Fixture host has not been created");
    }
    auto& state = *fixtureState();
    state.task_gate->release();
    for(const auto& settled : state.gated_settled) {
        settled.wait();
    }
    for(auto& helper : state.helpers) {
        if(helper.joinable()) {
            helper.join();
        }
    }
    state.helpers.clear();
}

void removeNamedTask(const std::string& name) {
    if(!fixtureState()) {
        throw std::runtime_error("Fixture host has not been created");
    }
    auto& registry = *fixtureState()->tasks;
    for(const auto& descriptor : registry.list()) {
        if(descriptor.name == name) {
            expect(registry.remove(descriptor.id));
            return;
        }
    }
    throw std::runtime_error("No task named " + name);
}

} // namespace

PYBIND11_MODULE(axiom_python_test_host, module) {
    module.def("create_host", &createHost);
    module.def("release_task_gate", &releaseTaskGate);
    module.def("schedule_invoke_gate_release", &scheduleInvokeGateRelease);
    module.def("wait_tasks_settled", &waitTasksSettled);
    module.def("remove_named_task", &removeNamedTask);
}
