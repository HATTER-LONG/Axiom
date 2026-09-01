#include <axiom/introspection/introspection_service.hpp>

#include <axiom/action/action_id.hpp>
#include <axiom/action/module.hpp>
#include <axiom/action/module_builder.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/async/executor.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/introspection/runtime_snapshot.hpp>
#include <axiom/resource/resource_id.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/resource/resource_traits.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_registry.hpp>
#include <axiom/task/task_types.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <latch>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct Widget final {};

} // namespace

template <> struct axiom::resource::ResourceTraits<::Widget> {
    static constexpr std::string_view type_name = "widget";
};

namespace {

axiom::ActionId actionId(const std::string_view text) {
    const auto parsed = axiom::ActionId::parse(text);
    EXPECT_TRUE(parsed);
    return parsed.value();
}

struct RuntimeFixture {
    axiom::Runtime runtime;
    axiom::resource::ResourceRegistry resources;
    axiom::task::TaskRegistry tasks;

    RuntimeFixture() {
        axiom::ModuleBuilder alpha{{.namespace_name = "alpha",
                                    .description = "Alpha module",
                                    .version = "1.0",
                                    .tags = {"alpha", "test"},
                                    .metadata = {{"kind", "test"}, {"owner", "suite"}}}};
        EXPECT_TRUE(alpha.add(
            "lookup", "Lookup",
            [](const std::map<std::string, int>& value) { return static_cast<int>(value.size()); },
            axiom::param("value", "Map")));
        EXPECT_TRUE(alpha.add("ping", "Ping", [] { return 1; }));
        EXPECT_TRUE(alpha.add(
            "vector", "Vector",
            [](const std::vector<int>& values) { return static_cast<int>(values.size()); },
            axiom::param("values", "Values")));
        EXPECT_TRUE(runtime.registerModule(std::move(alpha)));

        axiom::ModuleBuilder alphabet{{.namespace_name = "alphabet", .metadata = {}}};
        EXPECT_TRUE(alphabet.add("ping", "Ping", [] { return 2; }));
        EXPECT_TRUE(runtime.registerModule(std::move(alphabet)));
    }
};

void expectSequentialSnapshot(const axiom::introspection::RuntimeSnapshot& snapshot) {
    EXPECT_FALSE(snapshot.modules.empty());
    EXPECT_TRUE(
        std::ranges::is_sorted(snapshot.modules, {}, &axiom::ModuleDescriptor::namespace_name));
    for(const auto& module : snapshot.modules) {
        EXPECT_FALSE(module.namespace_name.empty());
    }
    EXPECT_FALSE(snapshot.actions.empty());
    EXPECT_TRUE(std::ranges::is_sorted(snapshot.actions, {},
                                       [](const auto& action) { return action.id.str(); }));
    for(const auto& action : snapshot.actions) {
        EXPECT_FALSE(action.id.str().empty());
    }
    for(const auto& resource : snapshot.resources) {
        EXPECT_EQ(resource.type, "widget");
        EXPECT_TRUE(axiom::resource::ResourceId::parse(resource.id.str()));
    }
    for(const auto& task : snapshot.tasks) {
        EXPECT_TRUE(axiom::task::TaskId::parse(task.id.str()));
        EXPECT_GE(task.progress.value, 0.0);
        EXPECT_LE(task.progress.value, 1.0);
        EXPECT_TRUE(std::isfinite(task.progress.value));
        if(task.error.has_value()) {
            EXPECT_TRUE(axiom::task::isTerminal(task.state));
        }
    }
}

TEST(IntrospectionService, ListsAndFiltersWithIndependentValues) {
    const RuntimeFixture fixture;
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};

    const auto modules = service.modules();
    ASSERT_EQ(modules.size(), 2U);
    EXPECT_EQ(modules[0].namespace_name, "alpha");
    EXPECT_EQ(modules[0].description, "Alpha module");
    EXPECT_EQ(modules[0].version, "1.0");
    ASSERT_EQ(modules[0].tags.size(), 2U);
    EXPECT_EQ(modules[0].tags[0], "alpha");
    EXPECT_EQ(modules[0].tags[1], "test");
    auto metadata = modules[0].metadata.begin();
    ASSERT_NE(metadata, modules[0].metadata.end());
    EXPECT_EQ(metadata->first, "kind");
    ++metadata;
    ASSERT_NE(metadata, modules[0].metadata.end());
    EXPECT_EQ(metadata->first, "owner");
    EXPECT_EQ(modules[1].namespace_name, "alphabet");

    const auto all = service.actions();
    ASSERT_EQ(all.size(), 4U);
    EXPECT_EQ(all[0].id.str(), "alpha.lookup");
    EXPECT_EQ(all[1].id.str(), "alpha.ping");
    EXPECT_EQ(all[2].id.str(), "alpha.vector");
    EXPECT_EQ(all[3].id.str(), "alphabet.ping");
    ASSERT_EQ(all[0].parameters.size(), 1U);
    ASSERT_NE(all[0].parameters[0].type.value_type, nullptr);
    const auto again = service.actions();
    ASSERT_NE(again[0].parameters[0].type.value_type, nullptr);
    EXPECT_NE(all[0].parameters[0].type.value_type.get(),
              again[0].parameters[0].type.value_type.get());
    const auto invoked = fixture.runtime.invoke(
        actionId("alpha.vector"),
        axiom::Arguments{{"values", axiom::Value{axiom::Value::Array{axiom::Value{1}}}}});
    ASSERT_TRUE(invoked);
    EXPECT_EQ(invoked.value().asInteger(), 1);

    EXPECT_EQ(service.actions("alpha").size(), 3U);
    EXPECT_EQ(service.actions("alph").size(), 0U);
    EXPECT_EQ(service.actions("ALPHA").size(), 0U);
}

TEST(IntrospectionService, DescribesAndForwardsNotFound) {
    RuntimeFixture fixture;
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    const auto action = service.describeAction(actionId("alpha.lookup"));
    ASSERT_TRUE(action);
    EXPECT_EQ(action.value().description, "Lookup");
    const auto missing_action = service.describeAction(actionId("alpha.missing"));
    ASSERT_FALSE(missing_action);
    EXPECT_EQ(missing_action.error().code, axiom::ErrorCode::NotFound);

    const auto resource = fixture.resources.add(std::make_unique<Widget>());
    ASSERT_TRUE(resource);
    const auto described = service.describeResource(resource.value().id());
    ASSERT_TRUE(described);
    EXPECT_EQ(described.value().type, "widget");
    const auto missing_resource_id = axiom::resource::ResourceId::parse("widget:999999");
    ASSERT_TRUE(missing_resource_id);
    const auto missing_resource = service.describeResource(missing_resource_id.value());
    ASSERT_FALSE(missing_resource);
    EXPECT_EQ(missing_resource.error().code, axiom::ErrorCode::NotFound);

    const auto missing_task_id = axiom::task::TaskId::parse("task:999999");
    ASSERT_TRUE(missing_task_id);
    const auto missing_task = service.describeTask(missing_task_id.value());
    ASSERT_FALSE(missing_task);
    EXPECT_EQ(missing_task.error().code, axiom::ErrorCode::NotFound);
}

TEST(IntrospectionService, FiltersResourcesAndPreservesTaskValues) {
    RuntimeFixture fixture;
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    const auto first = fixture.resources.add(std::make_unique<Widget>());
    ASSERT_TRUE(first);
    const auto all = service.resources();
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(all.front().type, "widget");
    ASSERT_TRUE(service.resources("widget"));
    EXPECT_EQ(service.resources("widget").value().size(), 1U);
    const auto invalid = service.resources("Widget");
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, axiom::ErrorCode::InvalidArgument);
    EXPECT_TRUE(service.resources("missing").value().empty());

    axiom::async::Executor executor{1};
    const auto submitted = fixture.tasks.submit(
        executor, "work", [](axiom::task::TaskContext&) { return axiom::Result<int>::success(7); });
    ASSERT_TRUE(submitted);
    executor.close();
    const auto tasks = service.tasks();
    ASSERT_EQ(tasks.size(), 1U);
    EXPECT_EQ(tasks.front().name, "work");
    const auto task = service.describeTask(tasks.front().id);
    ASSERT_TRUE(task);
    EXPECT_EQ(task.value().id, tasks.front().id);
}

TEST(IntrospectionService, ForwardsTaskProgressFailureAndErrorValues) {
    RuntimeFixture fixture;
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    axiom::async::Executor executor{1};
    const auto submitted = fixture.tasks.submit(
        executor, "fragile", [](axiom::task::TaskContext& context) -> axiom::Result<int> {
            context.reportProgress(0.375, "phase two");
            return axiom::Result<int>::failure(
                {.code = axiom::ErrorCode::InvocationFailed,
                 .message = "deterministic task failure",
                 .path = std::string{"jobs[2]"},
                 .details = axiom::Value{axiom::Value::Object{
                     {"attempt", axiom::Value{2}}, {"retryable", axiom::Value{false}}}}});
        });
    ASSERT_TRUE(submitted);
    executor.close();

    const auto described = service.describeTask(submitted.value().id());
    ASSERT_TRUE(described);
    EXPECT_EQ(described.value().state, axiom::task::TaskState::Failed);
    EXPECT_DOUBLE_EQ(described.value().progress.value, 0.375);
    EXPECT_EQ(described.value().progress.message, "phase two");
    ASSERT_TRUE(described.value().error.has_value());
    const auto& error = *described.value().error;
    EXPECT_EQ(error.code, axiom::ErrorCode::InvocationFailed);
    EXPECT_EQ(error.message, "deterministic task failure");
    ASSERT_TRUE(error.path.has_value());
    EXPECT_EQ(*error.path, "jobs[2]");
    const auto* const details = error.details ? &error.details->asObject() : nullptr;
    ASSERT_NE(details, nullptr);
    EXPECT_EQ(details->at("attempt").asInteger(), 2);
    EXPECT_FALSE(details->at("retryable").asBoolean());
}

TEST(IntrospectionService, SnapshotOwnsAllCollectionsAndUsesStableOrder) {
    RuntimeFixture fixture;
    const auto resource = fixture.resources.add(std::make_unique<Widget>());
    ASSERT_TRUE(resource);
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    const auto snapshot = service.snapshot();
    ASSERT_EQ(snapshot.modules.size(), 2U);
    ASSERT_EQ(snapshot.actions.size(), 4U);
    ASSERT_EQ(snapshot.resources.size(), 1U);
    EXPECT_TRUE(snapshot.tasks.empty());
    EXPECT_EQ(snapshot.actions.front().id.str(), "alpha.lookup");
    EXPECT_EQ(snapshot.resources.front().id, resource.value().id());

    ASSERT_TRUE(fixture.resources.remove(resource.value().id()));
    EXPECT_EQ(snapshot.resources.size(), 1U);
    EXPECT_EQ(service.resources().size(), 0U);
}

TEST(IntrospectionService, SnapshotObservesConcurrentSourcesIndependently) {
    RuntimeFixture fixture;
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    axiom::async::Executor executor{2};
    constexpr std::size_t rounds = 24U;
    std::latch ready{3};
    std::latch start{1};
    std::latch writers_done{3};

    const auto register_modules = [&fixture, &ready, &start, &writers_done] {
        ready.count_down();
        start.wait();
        for(std::size_t round = 0; round < rounds; ++round) {
            axiom::ModuleBuilder module{
                {.namespace_name = "concurrent_module_" + std::to_string(round), .metadata = {}}};
            EXPECT_TRUE(module.add("action", "Action", [] { return 1; }));
            EXPECT_TRUE(fixture.runtime.registerModule(std::move(module)));
        }
        writers_done.count_down();
    };
    const auto register_resources = [&fixture, &ready, &start, &writers_done] {
        ready.count_down();
        start.wait();
        for(std::size_t round = 0; round < rounds; ++round) {
            EXPECT_TRUE(fixture.resources.add(std::make_unique<Widget>()));
        }
        writers_done.count_down();
    };
    const auto submit_tasks = [&fixture, &executor, &ready, &start, &writers_done] {
        ready.count_down();
        start.wait();
        for(std::size_t round = 0; round < rounds; ++round) {
            const auto submitted = fixture.tasks.submit(
                executor, "concurrent-task",
                [](const axiom::task::TaskContext&) { return axiom::Result<void>::success(); });
            EXPECT_TRUE(submitted);
        }
        writers_done.count_down();
    };

    const std::jthread modules{register_modules};
    const std::jthread resources{register_resources};
    const std::jthread tasks{submit_tasks};
    ready.wait();
    start.count_down();
    for(std::size_t round = 0; round < rounds; ++round) {
        const auto snapshot = service.snapshot();

        expectSequentialSnapshot(snapshot);
        // These are independent source observations. The test intentionally does
        // not assert that counts or values came from one global point in time.
    }
    writers_done.wait();
    executor.close();
}

TEST(IntrospectionService, ReturnedDiscoveryValuesStayIndependentOfLaterSourceMutation) {
    RuntimeFixture fixture;
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    auto modules = service.modules();
    auto actions = service.actions();
    ASSERT_FALSE(modules.empty());
    ASSERT_FALSE(actions.empty());
    modules[0].description = "mutated";
    modules[0].metadata["kind"] = "changed";
    modules[0].tags.clear();
    actions[0].metadata["injected"] = "value";
    actions[0].tags.push_back("injected");

    const auto modules_again = service.modules();
    const auto actions_again = service.actions();
    ASSERT_FALSE(modules_again.empty());
    ASSERT_FALSE(actions_again.empty());
    EXPECT_EQ(modules_again[0].description, "Alpha module");
    EXPECT_EQ(modules_again[0].metadata.at("kind"), "test");
    ASSERT_EQ(modules_again[0].tags.size(), 2U);
    EXPECT_TRUE(actions_again[0].metadata.empty());
    EXPECT_TRUE(actions_again[0].tags.empty());

    const auto resource = fixture.resources.add(std::make_unique<Widget>());
    ASSERT_TRUE(resource);
    auto resources = service.resources();
    ASSERT_EQ(resources.size(), 1U);
    EXPECT_EQ(resources.front().type, "widget");
    resources.front().type = "forged";
    EXPECT_EQ(service.resources().front().type, "widget");

    axiom::async::Executor executor{1};
    const auto submitted =
        fixture.tasks.submit(executor, "origin-copy", [](const axiom::task::TaskContext&) {
            return axiom::Result<void>::success();
        });
    ASSERT_TRUE(submitted);
    executor.close();
    auto tasks = service.tasks();
    ASSERT_EQ(tasks.size(), 1U);
    EXPECT_FALSE(tasks.front().origin.has_value());
    tasks.front().origin = axiom::task::TaskOrigin{
        .request_id = "forged", .trace_id = {}, .caller = {}, .action_id = {}, .metadata = {}};
    const auto described = service.describeTask(tasks.front().id);
    ASSERT_TRUE(described);
    EXPECT_FALSE(described.value().origin.has_value());
}

} // namespace
