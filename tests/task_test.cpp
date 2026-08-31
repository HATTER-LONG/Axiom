#include <axiom/async/executor.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/logging/log_collector.hpp>
#include <axiom/logging/logging_service.hpp>
#include <axiom/resource/handle.hpp>
#include <axiom/resource/resource_traits.hpp>
#include <axiom/task/detail/task_control.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_registry.hpp>
#include <axiom/task/task_types.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct TaskShape {};
} // namespace
template <> struct axiom::resource::ResourceTraits<TaskShape> {
    static constexpr std::string_view type_name{"task_shape"};
};
namespace {
using axiom::Error;
using axiom::ErrorCode;
using axiom::Result;
using axiom::Value;
using axiom::async::Executor;
using axiom::task::TaskContext;
using axiom::task::TaskId;
using axiom::task::TaskRegistry;
using axiom::task::TaskState;

TEST(TaskId, ParsesOnlyCanonicalNonZeroSerials) {
    const auto valid = TaskId::parse("task:42");
    ASSERT_TRUE(valid);
    EXPECT_EQ(valid.value().str(), "task:42");
    EXPECT_EQ(valid.value(), TaskId::parse("task:42").value());
    EXPECT_NE(valid.value(), TaskId::parse("task:43").value());
    for(const auto invalid : {"", "task:", "task:0", "task:01", "Task:1", "task:-1", "task:1x"}) {
        EXPECT_EQ(TaskId::parse(invalid).error().code, ErrorCode::InvalidArgument);
    }
}

TEST(TaskId, HashesEqualCanonicalIdentities) {
    auto first = TaskId::parse("task:7").value();
    auto second = TaskId::parse("task:7").value();
    EXPECT_EQ(std::hash<TaskId>{}(first), std::hash<TaskId>{}(second));
    auto moved = std::move(first);
    EXPECT_EQ(moved.str(), "task:7");
    first = TaskId::parse("task:8").value();
    moved = first;
    EXPECT_EQ(moved, second = first);
}

TEST(TaskRegistry, CompletesValuesAndCopiesResults) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto submitted =
        tasks.submit(executor, "answer", [](TaskContext&) { return Result<int>::success(42); });
    ASSERT_TRUE(submitted);
    const auto handle = submitted.value();
    executor.close();
    EXPECT_EQ(handle.progress().value, 1.0);
    ASSERT_TRUE(handle.result());
    EXPECT_EQ(handle.result()->value(), 42);
    EXPECT_EQ(tasks.describe(handle.id()).value().name, "answer");
    auto copied = handle;
    copied = handle;
    auto moved = std::move(copied);
    EXPECT_TRUE(tasks.remove(moved.id()));
    EXPECT_EQ(tasks.describe(handle.id()).error().code, ErrorCode::NotFound);
    EXPECT_EQ(handle.result()->value(), 42);
    EXPECT_EQ(moved.result()->value(), 42);
}

TEST(TaskRegistry, PreservesValueResults) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto submitted = tasks.submit(executor, "value", [](TaskContext&) {
        return Result<Value>::success(Value{Value::Object{{"answer", Value{42}}}});
    });
    ASSERT_TRUE(submitted);
    executor.close();
    ASSERT_TRUE(submitted.value().result());
    EXPECT_EQ(submitted.value().result()->value().asObject().at("answer").asInteger(), 42);
}

TEST(TaskRegistry, PreservesResourceHandleResults) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto id = axiom::resource::ResourceId::parse("task_shape:1").value();
    const auto submitted = tasks.submit(executor, "resource", [id](TaskContext&) {
        return Result<axiom::resource::Handle<TaskShape>>::success(
            axiom::resource::Handle<TaskShape>{id});
    });
    ASSERT_TRUE(submitted);
    executor.close();
    ASSERT_TRUE(submitted.value().result());
    EXPECT_EQ(submitted.value().result()->value().id().str(), "task_shape:1");
}

TEST(TaskRegistry, AcceptsMoveOnlyCallablesAndCopiesProgressMessages) {
    Executor executor{1};
    TaskRegistry tasks;
    std::promise<void> running;
    std::promise<void> release;
    auto function = [owned = std::make_unique<int>(42), &running,
                     wait = release.get_future()](TaskContext& context) mutable {
        context.reportProgress(0.5, "owned");
        running.set_value();
        wait.wait();
        return Result<int>::success(*owned);
    };
    const auto submitted = tasks.submit(executor, "move-only", std::move(function));
    ASSERT_TRUE(submitted);
    running.get_future().wait();
    auto first = submitted.value().progress();
    auto second = submitted.value().progress();
    first.message += "-mutated";
    EXPECT_EQ(second.message, "owned");
    EXPECT_EQ(second.value, 0.5);
    release.set_value();
    executor.close();
    EXPECT_EQ(submitted.value().result()->value(), 42);
}

TEST(TaskRegistry, AllocatesUniqueIdentitiesAcrossRegistries) {
    Executor executor{1};
    TaskRegistry first;
    TaskRegistry second;
    const auto left =
        first.submit(executor, "left", [](TaskContext&) { return Result<void>::success(); });
    const auto right =
        second.submit(executor, "right", [](TaskContext&) { return Result<void>::success(); });
    ASSERT_TRUE(left);
    ASSERT_TRUE(right);
    EXPECT_NE(left.value().id(), right.value().id());
    executor.close();
}

TEST(TaskRegistry, ReportsFailureAndNormalizesExceptions) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto business = tasks.submit(executor, "failure", [](TaskContext&) {
        return Result<void>::failure({.code = ErrorCode::InvalidArgument,
                                      .message = "business",
                                      .path = std::nullopt,
                                      .details = std::nullopt});
    });
    const auto exception = tasks.submit(executor, "exception", [](TaskContext&) -> Result<void> {
        throw std::runtime_error{"hidden"};
    });
    ASSERT_TRUE(business);
    ASSERT_TRUE(exception);
    executor.close();
    EXPECT_EQ(business.value().result()->error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(exception.value().result()->error().code, ErrorCode::InvocationFailed);
    EXPECT_TRUE(tasks.remove(business.value().id()));
    EXPECT_TRUE(tasks.remove(exception.value().id()));
}

TEST(TaskRegistry, CancelsPendingTaskAndSkipsCallable) {
    axiom::logging::LoggingService logging;
    auto collector = std::make_shared<axiom::logging::LogCollector>();
    auto sink = logging.addSink(collector);
    Executor executor{1};
    TaskRegistry tasks{logging.logger("root")};
    std::promise<void> started;
    std::promise<void> release;
    const auto blocker = tasks.submit(
        executor, "blocker", [&started, wait = release.get_future()](TaskContext&) mutable {
            started.set_value();
            wait.wait();
            return Result<void>::success();
        });
    ASSERT_TRUE(blocker);
    started.get_future().wait();
    std::atomic_bool called{false};
    const auto pending = tasks.submit(executor, "pending", [&called](TaskContext&) {
        called.store(true);
        return Result<int>::success(1);
    });
    ASSERT_TRUE(pending);
    EXPECT_TRUE(tasks.cancel(pending.value().id()));
    EXPECT_TRUE(tasks.cancel(pending.value().id()));
    release.set_value();
    executor.close();
    EXPECT_EQ(pending.value().state(), TaskState::Cancelled);
    EXPECT_FALSE(called.load());
    EXPECT_EQ(pending.value().result()->error().code, ErrorCode::Cancelled);
    EXPECT_TRUE(tasks.remove(pending.value().id()));
    EXPECT_FALSE(collector->records().empty());
    static_cast<void>(sink);
}

TEST(TaskRegistry, ValidatesProgressAndPublishesOrderedSnapshots) {
    Executor executor{1};
    TaskRegistry tasks;
    std::vector<TaskState> observed;
    auto subscription = tasks.onChanged(
        [&observed](const auto& descriptor) { observed.push_back(descriptor.state); });
    const auto submitted = tasks.submit(executor, "progress", [](TaskContext& context) {
        context.reportProgress(0.25, "quarter");
        EXPECT_THROW(context.reportProgress(std::numeric_limits<double>::infinity()),
                     std::invalid_argument);
        EXPECT_THROW(context.reportProgress(-0.1), std::invalid_argument);
        EXPECT_THROW(context.reportProgress(1.1), std::invalid_argument);
        EXPECT_THROW(context.reportProgress(std::numeric_limits<double>::quiet_NaN()),
                     std::invalid_argument);
        return Result<void>::success();
    });
    ASSERT_TRUE(submitted);
    executor.close();
    ASSERT_EQ(observed.size(), 3U);
    EXPECT_EQ(observed[0], TaskState::Running);
    EXPECT_EQ(observed[1], TaskState::Running);
    EXPECT_EQ(observed[2], TaskState::Completed);
    EXPECT_TRUE(subscription.active());
}

TEST(TaskRegistry, RejectsUnknownAndActiveTaskOperations) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto unknown = TaskId::parse("task:999999").value();
    EXPECT_EQ(tasks.describe(unknown).error().code, ErrorCode::NotFound);
    EXPECT_EQ(tasks.cancel(unknown).error().code, ErrorCode::NotFound);
    EXPECT_EQ(tasks.remove(unknown).error().code, ErrorCode::NotFound);

    std::promise<void> release;
    std::promise<void> running;
    const auto submitted = tasks.submit(executor, "active", [&release, &running](TaskContext&) {
        running.set_value();
        release.get_future().wait();
        return Result<void>::success();
    });
    ASSERT_TRUE(submitted);
    running.get_future().wait();
    EXPECT_EQ(tasks.remove(submitted.value().id()).error().code, ErrorCode::InvalidArgument);
    EXPECT_TRUE(tasks.cancel(submitted.value().id()));
    EXPECT_TRUE(tasks.cancel(submitted.value().id()));
    release.set_value();
    executor.close();
}

TEST(TaskRegistry, RejectsClosedExecutorWithoutRegistration) {
    Executor executor{1};
    executor.close();
    TaskRegistry tasks;
    const auto submitted =
        tasks.submit(executor, "closed", [](TaskContext&) { return Result<int>::success(1); });
    EXPECT_EQ(submitted.error().code, ErrorCode::InvalidArgument);
    EXPECT_TRUE(tasks.list().empty());
}

TEST(TaskRegistry, RejectsEmptyChangeCallback) {
    TaskRegistry tasks;
    EXPECT_THROW(static_cast<void>(tasks.onChanged({})), std::invalid_argument);
}

TEST(TaskRegistry, AllowsReentrantChangeQueriesAndCancellation) {
    Executor executor{1};
    TaskRegistry tasks;
    std::promise<void> running;
    std::promise<void> release;
    std::vector<TaskState> observed;
    auto subscription = tasks.onChanged([&](const auto& descriptor) {
        observed.push_back(descriptor.state);
        EXPECT_TRUE(tasks.describe(descriptor.id));
        if(descriptor.state == TaskState::Running) {
            EXPECT_TRUE(tasks.cancel(descriptor.id));
        }
    });
    const auto submitted = tasks.submit(executor, "reentrant", [&](TaskContext& context) {
        running.set_value();
        release.get_future().wait();
        return context.cancellation().requested()
                   ? Result<void>::failure({.code = ErrorCode::Cancelled,
                                            .message = "cancelled",
                                            .path = std::nullopt,
                                            .details = std::nullopt})
                   : Result<void>::success();
    });
    ASSERT_TRUE(submitted);
    running.get_future().wait();
    release.set_value();
    executor.close();
    EXPECT_EQ(submitted.value().state(), TaskState::Cancelled);
    EXPECT_FALSE(observed.empty());
    static_cast<void>(subscription);
}

TEST(TaskRegistry, ListsPendingTasksInIdentityOrder) {
    Executor executor{1};
    TaskRegistry tasks;
    std::promise<void> release;
    std::promise<void> running;
    const auto first = tasks.submit(executor, "first", [&release, &running](TaskContext&) {
        running.set_value();
        release.get_future().wait();
        return Result<void>::success();
    });
    ASSERT_TRUE(first);
    running.get_future().wait();
    EXPECT_FALSE(first.value().result());
    const auto second =
        tasks.submit(executor, "second", [](TaskContext&) { return Result<void>::success(); });
    ASSERT_TRUE(second);
    const auto snapshot = tasks.list();
    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_LT(snapshot[0].id.str(), snapshot[1].id.str());
    release.set_value();
}

TEST(TaskRegistry, RunningCancellationIsCooperative) {
    Executor executor{1};
    TaskRegistry tasks;
    std::promise<void> running;
    std::promise<void> release;
    const auto submitted =
        tasks.submit(executor, "cooperative", [&running, &release](TaskContext& context) {
            running.set_value();
            release.get_future().wait();
            return context.cancellation().requested()
                       ? Result<int>::failure({.code = ErrorCode::Cancelled,
                                               .message = "noticed cancellation",
                                               .path = std::nullopt,
                                               .details = std::nullopt})
                       : Result<int>::success(1);
        });
    ASSERT_TRUE(submitted);
    running.get_future().wait();
    submitted.value().cancel();
    release.set_value();
    executor.close();
    EXPECT_EQ(submitted.value().state(), TaskState::Cancelled);
    EXPECT_TRUE(submitted.value().result()->hasError());
}

TEST(TaskRegistry, NormalizesUnknownExceptions) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto submitted =
        tasks.submit(executor, "unknown", [](TaskContext&) -> Result<int> { throw 1; });
    ASSERT_TRUE(submitted);
    executor.close();
    EXPECT_EQ(submitted.value().result()->error().code, ErrorCode::InternalError);
}

TEST(TaskRegistry, AddsTaskIdentityToBusinessLogContext) {
    axiom::logging::LoggingService logging;
    auto collector = std::make_shared<axiom::logging::LogCollector>();
    auto sink = logging.addSink(collector);
    Executor executor{1};
    TaskRegistry tasks{logging.logger("root")};
    const auto submitted = tasks.submit(executor, "logged", [&logging](TaskContext&) {
        AXIOM_LOG_INFO(logging.logger("business"), "business work");
        return Result<void>::success();
    });
    ASSERT_TRUE(submitted);
    executor.close();
    const auto records = collector->records();
    ASSERT_FALSE(records.empty());
    const auto business = std::find_if(records.begin(), records.end(), [](const auto& record) {
        return record.category == "business";
    });
    ASSERT_NE(business, records.end());
    EXPECT_TRUE(business->fields.contains("task_id"));
    EXPECT_EQ(business->fields.at("task_name").asString(), "logged");
    static_cast<void>(sink);
}

TEST(TaskRegistry, IsIndependentOfRegistryLifetimeAndIsolatesSubscriberFailure) {
    axiom::logging::LoggingService logging;
    auto collector = std::make_shared<axiom::logging::LogCollector>();
    auto sink = logging.addSink(collector);
    Executor executor{1};
    std::promise<void> release;
    std::promise<void> running;
    TaskRegistry::Subscription subscription;
    std::optional<axiom::task::TaskHandle<int>> handle;
    std::vector<TaskState> observed;
    std::size_t observed_before_destruction = 0U;
    {
        TaskRegistry tasks{logging.logger("root")};
        subscription = tasks.onChanged([](const auto&) { throw std::runtime_error{"subscriber"}; });
        auto good = tasks.onChanged(
            [&observed](const auto& descriptor) { observed.push_back(descriptor.state); });
        const auto submitted =
            tasks.submit(executor, "lifetime", [&release, &running](TaskContext&) {
                running.set_value();
                release.get_future().wait();
                return Result<int>::success(7);
            });
        ASSERT_TRUE(submitted);
        handle = submitted.value();
        running.get_future().wait();
        EXPECT_TRUE(good.active());
        observed_before_destruction = observed.size();
    }
    release.set_value();
    executor.close();
    EXPECT_EQ(handle->result()->value(), 7);
    EXPECT_FALSE(subscription.active());
    EXPECT_EQ(observed.size(), observed_before_destruction);
    EXPECT_TRUE(std::any_of(collector->records().begin(), collector->records().end(),
                            [](const auto& record) {
                                return record.message.find("callback failed") != std::string::npos;
                            }));
    static_cast<void>(sink);
}

TEST(TaskControl, SerializesNestedNotificationsAndKeepsTerminalState) {
    axiom::logging::LoggingService logging;
    auto collector = std::make_shared<axiom::logging::LogCollector>();
    auto sink = logging.addSink(collector);
    auto hub = std::make_shared<axiom::task::detail::NotificationHub>(logging.logger("notify"));
    auto control = std::make_shared<axiom::task::detail::TaskControl>(
        TaskId::parse("task:100000").value(), "nested", hub, logging.logger("task"),
        [] {
            return std::make_shared<const Result<int>>(
                Result<int>::failure({.code = ErrorCode::Cancelled,
                                      .message = "cancelled",
                                      .path = std::nullopt,
                                      .details = std::nullopt}));
        });
    auto subscription = hub->connect([&](const auto& descriptor) {
        if(descriptor.state == TaskState::Running && descriptor.progress.value == 0.0) {
            control->reportProgress(0.5, "nested");
        }
    });
    EXPECT_TRUE(control->start());
    EXPECT_FALSE(control->start());
    control->complete(TaskState::Completed,
                      std::make_shared<const Result<int>>(Result<int>::success(1)), std::nullopt);
    control->reportProgress(0.9, "late");
    control->complete(TaskState::Failed,
                      std::make_shared<const Result<int>>(
                          Result<int>::failure({.code = ErrorCode::InternalError,
                                                .message = "ignored",
                                                .path = std::nullopt,
                                                .details = std::nullopt})),
                      Error{.code = ErrorCode::InternalError,
                            .message = "ignored",
                            .path = std::nullopt,
                            .details = std::nullopt});
    EXPECT_EQ(control->state(), TaskState::Completed);
    EXPECT_EQ(control->progress().value, 1.0);
    EXPECT_EQ(control->progress().message, "nested");
    static_cast<void>(subscription);
    static_cast<void>(sink);
}
} // namespace
