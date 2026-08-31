#include <axiom/axiom.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace {
struct InstalledResource final {
    std::shared_ptr<int> value;
};
} // namespace

template <> struct axiom::resource::ResourceTraits<InstalledResource> {
    static constexpr std::string_view type_name = "installed";
};

namespace {
[[nodiscard]] bool checkInstalledContract(const bool passed, const char* name) {
    if(!passed) {
        std::cerr << "Installed contract failed: " << name << '\n';
    }
    return passed;
}

[[nodiscard]] bool invokeInstalledAction() {
    axiom::ModuleBuilder builder{{.namespace_name = "install", .metadata = {}}};
    if(!builder.add("answer", "Answer", [] { return 42; })) {
        return false;
    }
    axiom::Runtime runtime;
    if(!runtime.registerModule(std::move(builder))) {
        return false;
    }
    auto id = axiom::ActionId::parse("install.answer");
    if(!id) {
        return false;
    }
    auto result = runtime.invoke(id.value(), {});
    return result && result.value().asInteger() == 42;
}

[[nodiscard]] bool useInstalledAsync() {
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    auto original = scheduler.scheduleAfter(std::chrono::hours{1}, [] {});
    auto moved = std::move(original);
    axiom::async::Scheduler::ScheduleHandle handle;
    handle = std::move(moved);
    return handle.active() && handle.cancel() && !handle.active() &&
           executor.submit([] { return 42; }).get() == 42;
}

[[nodiscard]] bool checkInstalledTaskIdentity(const axiom::task::TaskRegistry& tasks,
                                              const axiom::task::TaskHandle<int>& handle) {
    const auto parsed = axiom::task::TaskId::parse(handle.id().str());
    const std::unordered_set<axiom::task::TaskId> ids{handle.id()};
    const auto descriptor = tasks.describe(handle.id());
    return parsed && ids.contains(parsed.value()) && descriptor && tasks.list().size() == 1;
}

[[nodiscard]] bool checkInstalledTaskCopies(const axiom::task::TaskHandle<int>& handle) {
    auto result = handle.result();
    if(!result || !*result || result->value() != 42) {
        return false;
    }
    result->value() = 0;
    const auto reread = handle.result();
    return reread && *reread && reread->value() == 42;
}

[[nodiscard]] bool useInstalledTaskResult() {
    axiom::async::Executor executor{1};
    axiom::task::TaskRegistry tasks;
    auto submitted = tasks.submit(executor, "installed answer",
                                  [answer = std::make_unique<int>(42)](axiom::task::TaskContext&) {
                                      return axiom::Result<int>::success(*answer);
                                  });
    if(!submitted) {
        return false;
    }
    auto handle = submitted.value();
    executor.close();
    if(!checkInstalledTaskIdentity(tasks, handle) || !tasks.remove(handle.id())) {
        return false;
    }
    return checkInstalledTaskCopies(handle) && tasks.list().empty() &&
           handle.state() == axiom::task::TaskState::Completed && handle.progress().value == 1.0;
}

[[nodiscard]] bool useInstalledVoidTask() {
    axiom::async::Executor executor{1};
    axiom::task::TaskRegistry tasks{axiom::logging::Logger{}};
    std::atomic<int> changes{0};
    auto subscription = tasks.onChanged([&](const axiom::task::TaskDescriptor&) { ++changes; });
    auto submitted = tasks.submit(executor, "installed void", [](axiom::task::TaskContext& ctx) {
        ctx.reportProgress(0.5, "Installed template progress");
        return axiom::Result<void>::success();
    });
    executor.close();
    if(!submitted) {
        return false;
    }
    const auto result = submitted.value().result();
    return result && result->hasValue() && changes == 3 && subscription.reset();
}

[[nodiscard]] bool useInstalledPendingCancellation() {
    axiom::async::Executor executor{1};
    std::promise<void> release;
    auto blocker = executor.submit([wait = release.get_future()] { wait.wait(); });
    axiom::task::TaskRegistry tasks;
    std::atomic<bool> called{false};
    auto submitted =
        tasks.submit(executor, "installed cancellation", [&called](axiom::task::TaskContext&) {
            called = true;
            return axiom::Result<int>::success(42);
        });
    if(!submitted) {
        release.set_value();
        return false;
    }
    submitted.value().cancel();
    const auto cancelled = submitted.value().result();
    release.set_value();
    executor.close();
    blocker.get();
    return cancelled && cancelled->hasError() &&
           cancelled->error().code == axiom::ErrorCode::Cancelled && !called;
}

[[nodiscard]] bool useInstalledResourceTask() {
    auto object = std::make_unique<InstalledResource>(InstalledResource{std::make_shared<int>(42)});
    const std::weak_ptr<int> lifetime = object->value;
    axiom::resource::ResourceRegistry resources;
    const auto resource = resources.add(std::move(object));
    if(!resource) {
        return false;
    }
    axiom::async::Executor executor{1};
    axiom::task::TaskRegistry tasks;
    const auto submitted = tasks.submit(executor, "installed resource",
                                        [handle = resource.value()](axiom::task::TaskContext&) {
                                            return axiom::Result<decltype(handle)>::success(handle);
                                        });
    executor.close();
    if(!submitted) {
        return false;
    }
    const auto result = submitted.value().result();
    if(!result || !*result || result->value().id() != resource.value().id()) {
        return false;
    }
    return resources.remove(resource.value().id()) && lifetime.expired();
}
} // namespace

int main() {
    return checkInstalledContract(axiom::isFrameworkName("Axiom"), "framework") &&
                   checkInstalledContract(invokeInstalledAction(), "action") &&
                   checkInstalledContract(useInstalledAsync(), "async") &&
                   checkInstalledContract(useInstalledTaskResult(), "task result") &&
                   checkInstalledContract(useInstalledVoidTask(), "void task notifications") &&
                   checkInstalledContract(useInstalledResourceTask(), "resource task lifetime") &&
                   checkInstalledContract(useInstalledPendingCancellation(), "pending cancellation")
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
