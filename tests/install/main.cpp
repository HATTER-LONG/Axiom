#include <axiom/axiom.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <utility>

namespace {
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
} // namespace

int main() {
    return axiom::isFrameworkName("Axiom") && invokeInstalledAction() && useInstalledAsync()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
