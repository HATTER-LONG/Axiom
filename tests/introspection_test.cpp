#include <axiom/introspection/introspection_query.hpp>
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
#include <future>
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
struct Geometry final {};

} // namespace

template <> struct axiom::resource::ResourceTraits<::Widget> {
    static constexpr std::string_view type_name = "widget";
};

template <> struct axiom::resource::ResourceTraits<::Geometry> {
    static constexpr std::string_view type_name = "geometry";
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

        axiom::ModuleBuilder alphabet{{.namespace_name = "alphabet",
                                       .description = {},
                                       .version = {},
                                       .tags = {},
                                       .metadata = {}}};
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

TEST(IntrospectionService, CopiesTaskOriginIndependently) {
    RuntimeFixture fixture;
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    axiom::async::Executor executor{1};
    axiom::task::TaskOrigin origin{.request_id = "intro-req",
                                   .trace_id = "intro-trace",
                                   .caller = "suite",
                                   .action_id = "alpha.ping",
                                   .metadata = {{"k", "v"}}};
    const auto submitted = fixture.tasks.submit(
        executor, axiom::task::TaskSubmission{.name = "origin", .origin = origin},
        [](axiom::task::TaskContext&) { return axiom::Result<int>::success(1); });
    ASSERT_TRUE(submitted);
    executor.close();
    auto described = service.describeTask(submitted.value().id());
    ASSERT_TRUE(described);
    ASSERT_TRUE(described.value().origin);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): ASSERT_TRUE already checked origin.
    EXPECT_EQ(described.value().origin->request_id, "intro-req");
    described.value().origin->request_id = "mutated";
    described.value().origin->metadata["k"] = "mutated";
    const auto again = service.describeTask(submitted.value().id());
    ASSERT_TRUE(again);
    ASSERT_TRUE(again.value().origin);
    EXPECT_EQ(again.value().origin->request_id, "intro-req");
    EXPECT_EQ(again.value().origin->metadata.at("k"), "v");
    const auto listed = service.tasks();
    ASSERT_EQ(listed.size(), 1U);
    ASSERT_TRUE(listed.front().origin);
    EXPECT_EQ(listed.front().origin->action_id, "alpha.ping");
    // NOLINTEND(bugprone-unchecked-optional-access)
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
    const auto& error = described.value().error;
    if(!error.has_value()) {
        FAIL() << "failed task should report an error";
        return;
    }
    EXPECT_EQ(error->code, axiom::ErrorCode::InvocationFailed);
    EXPECT_EQ(error->message, "deterministic task failure");
    if(!error->path.has_value()) {
        FAIL() << "failed task should report an error path";
        return;
    }
    EXPECT_EQ(*error->path, "jobs[2]");
    const auto* const details = error->details ? &error->details->asObject() : nullptr;
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
                {.namespace_name = "concurrent_module_" + std::to_string(round),
                 .description = {},
                 .version = {},
                 .tags = {},
                 .metadata = {}}};
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
    actions[0].tags.emplace_back("injected");

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
    EXPECT_EQ(resources.front().type, "forged");
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

void expectActionIds(const std::vector<axiom::ActionDescriptor>& actions,
                     const std::vector<std::string_view>& ids) {
    ASSERT_EQ(actions.size(), ids.size());
    for(std::size_t index = 0; index < ids.size(); ++index) {
        EXPECT_EQ(actions[index].id.str(), ids[index]);
    }
}

[[nodiscard]] axiom::task::TaskOrigin queryOrigin(std::string request_id, std::string action_id) {
    return {.request_id = std::move(request_id),
            .trace_id = {},
            .caller = {},
            .action_id = std::move(action_id),
            .metadata = {}};
}

[[nodiscard]] axiom::introspection::TaskQuery stateQuery(axiom::task::TaskState state) {
    return {.state = state, .origin_action_id = {}, .origin_request_id = {}};
}

[[nodiscard]] axiom::introspection::TaskQuery originQuery(std::optional<std::string> action_id,
                                                          std::optional<std::string> request_id) {
    return {.state = std::nullopt,
            .origin_action_id = std::move(action_id),
            .origin_request_id = std::move(request_id)};
}

struct CountDownOnExit {
    explicit CountDownOnExit(std::latch& latch) noexcept : latch_{&latch} {}
    void release() {
        if(latch_ != nullptr) {
            latch_->count_down();
            latch_ = nullptr;
        }
    }
    ~CountDownOnExit() { release(); }
    CountDownOnExit(const CountDownOnExit&) = delete;
    CountDownOnExit& operator=(const CountDownOnExit&) = delete;

private:
    std::latch* latch_{nullptr};
};

TEST(IntrospectionService, QueryFiltersActionsByExactModuleAndAllOfTags) {
    RuntimeFixture fixture;
    axiom::ModuleBuilder catalog{{.namespace_name = "catalog",
                                  .description = {},
                                  .version = {},
                                  .tags = {},
                                  .metadata = {}}};
    EXPECT_TRUE(catalog.add(
        "list", "List", [] { return 1; },
        axiom::ActionOptions{.version = {}, .tags = {"search"}, .metadata = {}}));
    EXPECT_TRUE(catalog.add(
        "search", "Search", [] { return 1; },
        axiom::ActionOptions{.version = {}, .tags = {"search", "index"}, .metadata = {}}));
    EXPECT_TRUE(fixture.runtime.registerModule(std::move(catalog)));
    axiom::ModuleBuilder other{
        {.namespace_name = "other", .description = {}, .version = {}, .tags = {}, .metadata = {}}};
    EXPECT_TRUE(other.add(
        "search", "Search", [] { return 1; },
        axiom::ActionOptions{.version = {}, .tags = {"search", "index"}, .metadata = {}}));
    EXPECT_TRUE(fixture.runtime.registerModule(std::move(other)));
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};

    expectActionIds(
        service.actions(axiom::introspection::ActionQuery{.module = "alpha", .tags = {}}),
        {"alpha.lookup", "alpha.ping", "alpha.vector"});
    EXPECT_TRUE(
        service.actions(axiom::introspection::ActionQuery{.module = "alph", .tags = {}}).empty());
    EXPECT_TRUE(
        service.actions(axiom::introspection::ActionQuery{.module = "ALPHA", .tags = {}}).empty());
    EXPECT_TRUE(service.actions(axiom::introspection::ActionQuery{.module = "missing", .tags = {}})
                    .empty());

    const auto empty_tags =
        service.actions(axiom::introspection::ActionQuery{.module = {}, .tags = {}});
    expectActionIds(empty_tags, {"alpha.lookup", "alpha.ping", "alpha.vector", "alphabet.ping",
                                 "catalog.list", "catalog.search", "other.search"});
    expectActionIds(
        service.actions(axiom::introspection::ActionQuery{.module = {}, .tags = {"search"}}),
        {"catalog.list", "catalog.search", "other.search"});
    expectActionIds(service.actions(axiom::introspection::ActionQuery{.module = {},
                                                                      .tags = {"search", "index"}}),
                    {"catalog.search", "other.search"});
    expectActionIds(service.actions(axiom::introspection::ActionQuery{
                        .module = {}, .tags = {"search", "search"}}),
                    {"catalog.list", "catalog.search", "other.search"});
    EXPECT_TRUE(
        service.actions(axiom::introspection::ActionQuery{.module = {}, .tags = {"missing"}})
            .empty());
    expectActionIds(service.actions(axiom::introspection::ActionQuery{.module = "catalog",
                                                                      .tags = {"search", "index"}}),
                    {"catalog.search"});
}

TEST(IntrospectionService, QueryResourceTypeKeepsCurrentErrorsAndEmptySuccess) {
    RuntimeFixture fixture;
    ASSERT_TRUE(fixture.resources.add(std::make_unique<Widget>()));
    ASSERT_TRUE(fixture.resources.add(std::make_unique<Geometry>()));
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    const auto widgets = service.resources(axiom::introspection::ResourceQuery{.type = "widget"});
    ASSERT_TRUE(widgets);
    ASSERT_EQ(widgets.value().size(), 1U);
    EXPECT_EQ(widgets.value().front().type, "widget");
    const auto geometry =
        service.resources(axiom::introspection::ResourceQuery{.type = "geometry"});
    ASSERT_TRUE(geometry);
    ASSERT_EQ(geometry.value().size(), 1U);
    EXPECT_EQ(geometry.value().front().type, "geometry");
    EXPECT_TRUE(
        service.resources(axiom::introspection::ResourceQuery{.type = "missing"}).value().empty());
    const auto illegal = service.resources(axiom::introspection::ResourceQuery{.type = "Widget"});
    ASSERT_FALSE(illegal);
    EXPECT_EQ(illegal.error().code, axiom::ErrorCode::InvalidArgument);
}

TEST(IntrospectionService, QueryFiltersTasksByStateWithoutResorting) {
    RuntimeFixture fixture;
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    axiom::async::Executor blocked{1};
    std::promise<void> release_blocked;
    auto hold = blocked.submit([wait = release_blocked.get_future()] { wait.wait(); });
    const auto pending =
        fixture.tasks.submit(blocked, "pending", [](const axiom::task::TaskContext&) {
            return axiom::Result<void>::success();
        });
    ASSERT_TRUE(pending);
    EXPECT_EQ(pending.value().state(), axiom::task::TaskState::Pending);
    ASSERT_EQ(service.tasks(stateQuery(axiom::task::TaskState::Pending)).size(), 1U);

    axiom::async::Executor live{2};
    std::latch running_started{1};
    std::latch release_running{1};
    CountDownOnExit release_on_exit{release_running};
    const auto running =
        fixture.tasks.submit(live,
                             axiom::task::TaskSubmission{
                                 .name = "running", .origin = queryOrigin("req-run", "alpha.ping")},
                             [&running_started, &release_running](const axiom::task::TaskContext&) {
                                 running_started.count_down();
                                 release_running.wait();
                                 return axiom::Result<void>::success();
                             });
    ASSERT_TRUE(running);
    running_started.wait();
    std::latch settled{2};
    ASSERT_TRUE(
        fixture.tasks.submit(live,
                             axiom::task::TaskSubmission{
                                 .name = "done", .origin = queryOrigin("req-shared", "alpha.ping")},
                             [&settled](const axiom::task::TaskContext&) {
                                 settled.count_down();
                                 return axiom::Result<void>::success();
                             }));
    ASSERT_TRUE(fixture.tasks.submit(
        live,
        axiom::task::TaskSubmission{.name = "fail",
                                    .origin = queryOrigin("req-shared", "alpha.lookup")},
        [&settled](const axiom::task::TaskContext&) {
            settled.count_down();
            return axiom::Result<void>::failure({.code = axiom::ErrorCode::InvocationFailed,
                                                 .message = "fail",
                                                 .path = std::nullopt,
                                                 .details = std::nullopt});
        }));
    settled.wait();
    pending.value().cancel();

    using axiom::task::TaskState;
    EXPECT_TRUE(service.tasks(stateQuery(TaskState::Pending)).empty());
    ASSERT_EQ(service.tasks(stateQuery(TaskState::Running)).size(), 1U);
    ASSERT_EQ(service.tasks(stateQuery(TaskState::Completed)).front().name, "done");
    EXPECT_EQ(service.tasks(stateQuery(TaskState::Failed)).front().name, "fail");
    EXPECT_EQ(service.tasks(stateQuery(TaskState::Cancelled)).front().name, "pending");

    release_on_exit.release();
    release_blocked.set_value();
    hold.get();
    live.close();
    blocked.close();
}

TEST(IntrospectionService, QueryFiltersTasksByOriginWithoutResorting) {
    RuntimeFixture fixture;
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    axiom::async::Executor live{2};
    std::latch running_started{1};
    std::latch release_running{1};
    CountDownOnExit release_on_exit{release_running};
    std::latch settled{2};
    ASSERT_TRUE(
        fixture.tasks.submit(live,
                             axiom::task::TaskSubmission{
                                 .name = "running", .origin = queryOrigin("req-run", "alpha.ping")},
                             [&running_started, &release_running](const axiom::task::TaskContext&) {
                                 running_started.count_down();
                                 release_running.wait();
                                 return axiom::Result<void>::success();
                             }));
    running_started.wait();
    ASSERT_TRUE(
        fixture.tasks.submit(live,
                             axiom::task::TaskSubmission{
                                 .name = "done", .origin = queryOrigin("req-shared", "alpha.ping")},
                             [&settled](const axiom::task::TaskContext&) {
                                 settled.count_down();
                                 return axiom::Result<void>::success();
                             }));
    ASSERT_TRUE(fixture.tasks.submit(
        live,
        axiom::task::TaskSubmission{.name = "fail",
                                    .origin = queryOrigin("req-shared", "alpha.lookup")},
        [&settled](const axiom::task::TaskContext&) {
            settled.count_down();
            return axiom::Result<void>::failure({.code = axiom::ErrorCode::InvocationFailed,
                                                 .message = "fail",
                                                 .path = std::nullopt,
                                                 .details = std::nullopt});
        }));
    settled.wait();

    using axiom::task::TaskState;
    const auto by_action = service.tasks(originQuery("alpha.ping", {}));
    ASSERT_EQ(by_action.size(), 2U);
    EXPECT_EQ(by_action[0].name, "running");
    EXPECT_EQ(by_action[1].name, "done");
    const auto by_request = service.tasks(originQuery({}, "req-shared"));
    ASSERT_EQ(by_request.size(), 2U);
    EXPECT_EQ(by_request[0].name, "done");
    EXPECT_EQ(by_request[1].name, "fail");
    const auto combined = service.tasks({.state = TaskState::Failed,
                                         .origin_action_id = "alpha.lookup",
                                         .origin_request_id = "req-shared"});
    ASSERT_EQ(combined.size(), 1U);
    EXPECT_EQ(combined.front().name, "fail");
    EXPECT_TRUE(service.tasks(originQuery("missing.action", {})).empty());
    EXPECT_TRUE(service.tasks(originQuery({}, "unknown")).empty());

    release_on_exit.release();
    live.close();
}

TEST(IntrospectionService, QueryOverloadsMatchLegacyFiltersAndLeaveSnapshotUnfiltered) {
    RuntimeFixture fixture;
    ASSERT_TRUE(fixture.resources.add(std::make_unique<Widget>()));
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    const auto by_view = service.actions("alpha");
    const auto by_query =
        service.actions(axiom::introspection::ActionQuery{.module = "alpha", .tags = {}});
    expectActionIds(by_view, {"alpha.lookup", "alpha.ping", "alpha.vector"});
    expectActionIds(by_query, {"alpha.lookup", "alpha.ping", "alpha.vector"});
    EXPECT_TRUE(service.actions("alph").empty());
    EXPECT_TRUE(
        service.actions(axiom::introspection::ActionQuery{.module = "alph", .tags = {}}).empty());

    const auto typed = service.resources("widget");
    const auto queried = service.resources(axiom::introspection::ResourceQuery{.type = "widget"});
    ASSERT_TRUE(typed);
    ASSERT_TRUE(queried);
    ASSERT_EQ(typed.value().size(), queried.value().size());
    EXPECT_EQ(typed.value().front().id, queried.value().front().id);
    const auto illegal_view = service.resources("Widget");
    const auto illegal_query =
        service.resources(axiom::introspection::ResourceQuery{.type = "Widget"});
    ASSERT_FALSE(illegal_view);
    ASSERT_FALSE(illegal_query);
    EXPECT_EQ(illegal_view.error().code, illegal_query.error().code);
    EXPECT_EQ(illegal_view.error().message, illegal_query.error().message);

    const auto snapshot = service.snapshot();
    EXPECT_EQ(snapshot.actions.size(), service.actions().size());
    EXPECT_EQ(snapshot.resources.size(), service.resources().size());
}

TEST(IntrospectionService, QueryResultsStayInternallyConsistentDuringSourceChanges) {
    RuntimeFixture fixture;
    const axiom::introspection::IntrospectionService service{fixture.runtime, fixture.resources,
                                                             fixture.tasks};
    axiom::async::Executor executor{2};
    constexpr std::size_t rounds = 16U;
    std::latch ready{3};
    std::latch start{1};
    std::latch writers_done{3};
    const std::jthread modules{[&fixture, &ready, &start, &writers_done] {
        ready.count_down();
        start.wait();
        for(std::size_t round = 0; round < rounds; ++round) {
            axiom::ModuleBuilder module{{.namespace_name = "query_module_" + std::to_string(round),
                                         .description = {},
                                         .version = {},
                                         .tags = {},
                                         .metadata = {}}};
            EXPECT_TRUE(module.add(
                "action", "Action", [] { return 1; },
                axiom::ActionOptions{.version = {}, .tags = {"live"}, .metadata = {}}));
            EXPECT_TRUE(fixture.runtime.registerModule(std::move(module)));
        }
        writers_done.count_down();
    }};
    const std::jthread resources{[&fixture, &ready, &start, &writers_done] {
        ready.count_down();
        start.wait();
        for(std::size_t round = 0; round < rounds; ++round) {
            EXPECT_TRUE(fixture.resources.add(std::make_unique<Widget>()));
        }
        writers_done.count_down();
    }};
    const std::jthread tasks{[&fixture, &executor, &ready, &start, &writers_done] {
        ready.count_down();
        start.wait();
        for(std::size_t round = 0; round < rounds; ++round) {
            const auto submitted = fixture.tasks.submit(
                executor,
                axiom::task::TaskSubmission{.name = "query-task",
                                            .origin = axiom::task::TaskOrigin{.request_id = "q",
                                                                              .trace_id = {},
                                                                              .caller = {},
                                                                              .action_id = {},
                                                                              .metadata = {}}},
                [](const axiom::task::TaskContext&) { return axiom::Result<void>::success(); });
            EXPECT_TRUE(submitted);
        }
        writers_done.count_down();
    }};
    ready.wait();
    start.count_down();
    for(std::size_t round = 0; round < rounds; ++round) {
        const auto actions =
            service.actions(axiom::introspection::ActionQuery{.module = {}, .tags = {"live"}});
        for(const auto& action : actions) {
            EXPECT_EQ(action.id.action(), "action");
            ASSERT_FALSE(action.tags.empty());
            EXPECT_EQ(action.tags.front(), "live");
        }
        const auto resources_found =
            service.resources(axiom::introspection::ResourceQuery{.type = "widget"});
        ASSERT_TRUE(resources_found);
        for(const auto& resource : resources_found.value()) {
            EXPECT_EQ(resource.type, "widget");
        }
        const auto tasks_found = service.tasks(originQuery({}, "q"));
        for(const auto& task : tasks_found) {
            ASSERT_TRUE(task.origin);
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access): ASSERT_TRUE checked origin.
            EXPECT_EQ(task.origin->request_id, "q");
            EXPECT_TRUE(axiom::task::TaskId::parse(task.id.str()));
        }
    }
    writers_done.wait();
    executor.close();
}

} // namespace
