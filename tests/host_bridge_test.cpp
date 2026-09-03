#include <axiom/action/action_id.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/action/module_builder.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/command/command_methods.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/python/host_bridge.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/task/task_registry.hpp>
#ifdef AXIOM_ENABLE_TEST_SEAMS
#include <python/dispatch_fault.hpp>
#endif

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <initializer_list>
#include <latch>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using axiom::ErrorCode;
using axiom::InvocationContext;
using axiom::ModuleBuilder;
using axiom::Result;
using axiom::Runtime;
using axiom::Value;
using axiom::command::action_describe;
using axiom::command::action_list;
using axiom::python::HostBridge;
using axiom::python::HostHandle;
using axiom::resource::ResourceRegistry;
using axiom::task::TaskRegistry;

[[nodiscard]] Value::Object object(std::initializer_list<Value::Object::value_type> fields) {
    return Value::Object{fields};
}

void expectHostClosed(const Result<Value>& result, const std::string_view message) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::HostClosed);
    EXPECT_EQ(result.error().message, message);
    EXPECT_EQ(result.error().path, std::nullopt);
    EXPECT_FALSE(result.error().details.has_value());
}

struct BridgeFixture {
    Runtime runtime;
    ResourceRegistry resources;
    TaskRegistry tasks;
    std::shared_ptr<InvocationContext> observed = std::make_shared<InvocationContext>();

    BridgeFixture() {
        ModuleBuilder math{{.namespace_name = "math",
                            .description = "Math",
                            .version = {},
                            .tags = {},
                            .metadata = {}}};
        EXPECT_TRUE(math.add("ping", "Ping", [] { return 1; }));
        EXPECT_TRUE(math.addContextual(
            "probe", "Probe", [observed = observed](const axiom::ActionInvocation& invocation) {
                *observed = invocation.context();
                return 1;
            }));
        EXPECT_TRUE(runtime.registerModule(std::move(math)));
    }
};

} // namespace

TEST(HostBridge, DispatchesThroughBridgeAndHandlesAndForwardsContext) {
    BridgeFixture fixture;
    const HostBridge bridge{fixture.runtime, fixture.resources, fixture.tasks};
    const auto listed = bridge.dispatch(action_list, {}, {});
    ASSERT_TRUE(listed);
    EXPECT_EQ(listed.value().asArray().size(), 2U);
    const HostHandle handle = bridge.attach();
    ASSERT_FALSE(handle.empty());
    const auto described =
        handle.dispatch(action_describe, object({{"action", Value{"math.ping"}}}), {});
    ASSERT_TRUE(described);
    EXPECT_EQ(described.value().asObject().at("id").asString(), "math.ping");
    const InvocationContext context{
        .request_id = "req", .trace_id = "trace", .caller = "suite", .metadata = {{"k", "v"}}};
    const auto probed = handle.dispatch(
        axiom::command::action_invoke,
        object({{"action", Value{"math.probe"}}, {"arguments", Value{object({})}}}), context);
    ASSERT_TRUE(probed);
    EXPECT_EQ(fixture.observed->request_id, "req");
    EXPECT_EQ(fixture.observed->trace_id, "trace");
    EXPECT_EQ(fixture.observed->caller, "suite");
    EXPECT_EQ(fixture.observed->metadata,
              (std::map<std::string, std::string, std::less<>>{{"k", "v"}}));
}

TEST(HostBridge, EmptyHandleAndMovedFromBridgeFailAsNotAttached) {
    const HostHandle empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_TRUE(empty.closed());
    expectHostClosed(empty.dispatch(action_list, {}, {}), "Host session is not attached");
    BridgeFixture fixture;
    HostBridge bridge{fixture.runtime, fixture.resources, fixture.tasks};
    HostBridge moved{std::move(bridge)};
    // NOLINTBEGIN(bugprone-use-after-move): moved-from behavior is part of the contract.
    EXPECT_TRUE(bridge.closed());
    expectHostClosed(bridge.dispatch(action_list, {}, {}), "Host session is not attached");
    // NOLINTEND(bugprone-use-after-move)
    EXPECT_FALSE(moved.closed());
    EXPECT_TRUE(moved.dispatch(action_list, {}, {}));
}

TEST(HostBridge, MoveAssignmentClosesTheDisplacedSession) {
    BridgeFixture fixture;
    HostBridge first{fixture.runtime, fixture.resources, fixture.tasks};
    const HostHandle handle = first.attach();
    HostBridge second{fixture.runtime, fixture.resources, fixture.tasks};
    first = std::move(second);
    expectHostClosed(handle.dispatch(action_list, {}, {}), "Host session is closed");
    EXPECT_TRUE(first.dispatch(action_list, {}, {}));
    // NOLINTNEXTLINE(bugprone-use-after-move): moved-from dispatch must fail deterministically.
    expectHostClosed(second.dispatch(action_list, {}, {}), "Host session is not attached");
}

TEST(HostBridge, CloseIsIdempotentAndRejectsNewDispatch) {
    BridgeFixture fixture;
    HostBridge bridge{fixture.runtime, fixture.resources, fixture.tasks};
    const HostHandle handle = bridge.attach();
    EXPECT_TRUE(bridge.close());
    EXPECT_TRUE(bridge.close());
    EXPECT_TRUE(bridge.closed());
    EXPECT_TRUE(handle.closed());
    expectHostClosed(bridge.dispatch(action_list, {}, {}), "Host session is closed");
    expectHostClosed(handle.dispatch(action_list, {}, {}), "Host session is closed");
    const HostHandle later = bridge.attach();
    EXPECT_TRUE(later.closed());
    EXPECT_TRUE(fixture.runtime.invoke(axiom::ActionId::parse("math.ping").value(), {}, {}));
}

TEST(HostBridge, CloseWaitsForInFlightDispatchWithoutTouchingSources) {
    BridgeFixture fixture;
    std::latch ridden{1};
    std::promise<void> release;
    ModuleBuilder slow{{.namespace_name = "slow",
                        .description = "Slow",
                        .version = {},
                        .tags = {},
                        .metadata = {}}};
    auto* const entered = &ridden;
    auto gate = release.get_future().share();
    EXPECT_TRUE(slow.add("ride", "Ride", [entered, gate] {
        entered->count_down();
        gate.wait();
        return 7;
    }));
    EXPECT_TRUE(fixture.runtime.registerModule(std::move(slow)));
    {
        HostBridge bridge{fixture.runtime, fixture.resources, fixture.tasks};
        const HostHandle handle = bridge.attach();
        auto in_flight = std::async(std::launch::async, [&handle] {
            return handle.dispatch(
                axiom::command::action_invoke,
                object({{"action", Value{"slow.ride"}}, {"arguments", Value{object({})}}}), {});
        });
        ridden.wait();
        auto closed = std::async(std::launch::async, [&bridge] { bridge.close(); });
        release.set_value();
        const auto result = in_flight.get();
        ASSERT_TRUE(result);
        EXPECT_EQ(result.value().asInteger(), 7);
        closed.get();
        expectHostClosed(handle.dispatch(action_list, {}, {}), "Host session is closed");
    }
    EXPECT_TRUE(fixture.runtime.invoke(axiom::ActionId::parse("math.ping").value(), {}, {}));
}

TEST(HostBridge, ReentrantCloseIsRejectedInsteadOfDeadlocking) {
    BridgeFixture fixture;
    HostBridge* current_bridge = nullptr;
    ModuleBuilder reentrant{{.namespace_name = "reentrant",
                             .description = "Reentrant",
                             .version = {},
                             .tags = {},
                             .metadata = {}}};
    ASSERT_TRUE(reentrant.add("close", "Attempts to close its active bridge",
                              [&current_bridge] { return current_bridge->close(); }));
    ASSERT_TRUE(fixture.runtime.registerModule(std::move(reentrant)));
    HostBridge bridge{fixture.runtime, fixture.resources, fixture.tasks};
    current_bridge = &bridge;

    const auto result = bridge.dispatch(
        axiom::command::action_invoke,
        object({{"action", Value{"reentrant.close"}}, {"arguments", Value{object({})}}}), {});
    ASSERT_TRUE(result);
    EXPECT_FALSE(result.value().asBoolean());
    EXPECT_FALSE(bridge.closed());
    EXPECT_TRUE(bridge.close());
}

TEST(HostBridge, CoreNormalizedActionFailureStillAllowsClose) {
    BridgeFixture fixture;
    std::latch entered{1};
    std::promise<void> release;
    ModuleBuilder faulty{{.namespace_name = "fault",
                          .description = "Fault",
                          .version = {},
                          .tags = {},
                          .metadata = {}}};
    auto gate = release.get_future().share();
    EXPECT_TRUE(faulty.add("throw", "Throws after release", [&entered, gate]() -> int {
        entered.count_down();
        gate.wait();
        throw std::bad_alloc{};
    }));
    EXPECT_TRUE(fixture.runtime.registerModule(std::move(faulty)));
    HostBridge bridge{fixture.runtime, fixture.resources, fixture.tasks};
    const HostHandle handle = bridge.attach();
    auto in_flight = std::async(std::launch::async, [&handle] {
        return handle.dispatch(
            axiom::command::action_invoke,
            object({{"action", Value{"fault.throw"}}, {"arguments", Value{object({})}}}), {});
    });
    entered.wait();
    auto closed = std::async(std::launch::async, [&bridge] { bridge.close(); });
    release.set_value();
    const auto result = in_flight.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::InvocationFailed);
    // Core normalizes the Action's exception into a Result failure, so the
    // dispatch returns normally; close() must still drain the lease promptly.
    ASSERT_EQ(closed.wait_for(std::chrono::seconds{5}), std::future_status::ready);
    closed.get();
    expectHostClosed(handle.dispatch(action_list, {}, {}), "Host session is closed");
    EXPECT_TRUE(fixture.runtime.invoke(axiom::ActionId::parse("math.ping").value(), {}, {}));
}

#ifdef AXIOM_ENABLE_TEST_SEAMS
TEST(HostBridge, DispatcherThrowingWhileLeasedStillLetsCloseComplete) {
    BridgeFixture fixture;
    HostBridge bridge{fixture.runtime, fixture.resources, fixture.tasks};
    const HostHandle handle = bridge.attach();
    {
        const axiom::python::detail::DispatchFaultGuard fault;
        EXPECT_THROW(static_cast<void>(handle.dispatch(action_list, {}, {})), std::runtime_error);
    }
    // The fault throws after the lease is acquired, so the lease must be
    // released on the exception unwind; a leaked lease would make close()
    // wait forever.
    auto closed = std::async(std::launch::async, [&bridge] { bridge.close(); });
    ASSERT_EQ(closed.wait_for(std::chrono::seconds{5}), std::future_status::ready);
    closed.get();
    EXPECT_TRUE(handle.closed());
    expectHostClosed(handle.dispatch(action_list, {}, {}), "Host session is closed");
    EXPECT_TRUE(fixture.runtime.invoke(axiom::ActionId::parse("math.ping").value(), {}, {}));
}

TEST(HostBridge, OverlappingDispatchFaultGuardsStayArmedUntilLastRelease) {
    BridgeFixture fixture;
    HostBridge bridge{fixture.runtime, fixture.resources, fixture.tasks};
    std::optional<axiom::python::detail::DispatchFaultGuard> first;
    std::optional<axiom::python::detail::DispatchFaultGuard> second;
    first.emplace();
    second.emplace();
    first.reset();
    EXPECT_THROW(static_cast<void>(bridge.dispatch(action_list, {}, {})), std::runtime_error);
    second.reset();
    EXPECT_TRUE(bridge.dispatch(action_list, {}, {}));
}
#endif

TEST(HostBridge, HandlesOutliveTheBridgeAndFailClosed) {
    BridgeFixture fixture;
    HostHandle handle;
    {
        const HostBridge bridge{fixture.runtime, fixture.resources, fixture.tasks};
        handle = bridge.attach();
        const HostHandle copy = handle;
        EXPECT_FALSE(copy.closed());
    }
    EXPECT_TRUE(handle.closed());
    expectHostClosed(handle.dispatch(action_list, {}, {}), "Host session is closed");
}

namespace {

void raceDispatchWorker(HostBridge& bridge,
                        std::latch& start,
                        std::atomic<int>& outcomes,
                        const std::size_t iterations) {
    start.arrive_and_wait();
    for(std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const auto result = bridge.dispatch(action_list, {}, {});
        if(result) {
            ++outcomes;
        } else {
            EXPECT_EQ(result.error().code, ErrorCode::HostClosed);
        }
    }
}

struct RaceBounds {
    int workers;
    std::size_t iterations;
};

[[nodiscard]] int runCloseRace(HostBridge& bridge, const RaceBounds& bounds) {
    std::latch start{bounds.workers};
    std::atomic<int> outcomes{0};
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(bounds.workers));
    for(int worker = 0; worker < bounds.workers; ++worker) {
        threads.emplace_back([&bridge, &start, &outcomes, iterations = bounds.iterations] {
            raceDispatchWorker(bridge, start, outcomes, iterations);
        });
    }
    start.wait();
    bridge.close();
    for(auto& thread : threads) {
        thread.join();
    }
    return outcomes.load();
}

} // namespace

TEST(HostBridge, CloseRacesConcurrentDispatchesDeterministically) {
    BridgeFixture fixture;
    HostBridge bridge{fixture.runtime, fixture.resources, fixture.tasks};
    EXPECT_GE(runCloseRace(bridge, RaceBounds{.workers = 4, .iterations = 50}), 0);
    expectHostClosed(bridge.dispatch(action_list, {}, {}), "Host session is closed");
}

TEST(HostBridge, SourcesRemainUsableAfterBridgeDestruction) {
    BridgeFixture fixture;
    {
        const HostBridge bridge{fixture.runtime, fixture.resources, fixture.tasks};
        ASSERT_TRUE(bridge.dispatch(action_list, {}, {}));
    }
    const auto id = axiom::ActionId::parse("math.ping");
    ASSERT_TRUE(id);
    const auto invoked = fixture.runtime.invoke(id.value(), {}, {});
    ASSERT_TRUE(invoked);
    EXPECT_EQ(invoked.value().asInteger(), 1);
}
