#include <axiom/async/executor.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/logging/log_collector.hpp>
#include <axiom/logging/log_record.hpp>
#include <axiom/logging/logger.hpp>
#include <axiom/logging/logging_service.hpp>
#include <axiom/resource/handle.hpp>
#include <axiom/resource/resource_traits.hpp>
#include <axiom/task/detail/task_control.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_registry.hpp>
#include <axiom/task/task_types.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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
using axiom::task::TaskHandle;
using axiom::task::TaskId;
using axiom::task::TaskRegistry;
using axiom::task::TaskState;

[[nodiscard]] Error cancelledError(std::string message) {
    return {.code = ErrorCode::Cancelled,
            .message = std::move(message),
            .path = std::nullopt,
            .details = std::nullopt};
}

template <typename Exception, typename Callable> [[nodiscard]] bool throws(Callable&& callable) {
    try {
        std::forward<Callable>(callable)();
    } catch(const Exception&) {
        return true;
    } catch(...) {
        return false;
    }
    return false;
}

[[nodiscard]] bool reportProgressRejected(TaskContext& context, double value) {
    return throws<std::invalid_argument>([&context, value] { context.reportProgress(value); });
}

template <typename T> [[nodiscard]] const T* peekOptional(const std::optional<T>& value) {
    if(!value.has_value()) {
        return nullptr;
    }
    return std::addressof(*value);
}

[[nodiscard]] bool reportProgressContract(TaskContext& context) {
    context.reportProgress(0.25, "quarter");
    if(!reportProgressRejected(context, std::numeric_limits<double>::infinity())) {
        return false;
    }
    if(!reportProgressRejected(context, -0.1)) {
        return false;
    }
    if(!reportProgressRejected(context, 1.1)) {
        return false;
    }
    if(!reportProgressRejected(context, std::numeric_limits<double>::quiet_NaN())) {
        return false;
    }
    context.reportProgress(0.8, "almost");
    context.reportProgress(0.3, "rollback");
    return true;
}

[[nodiscard]] bool isRunningThenCompleted(const std::vector<TaskState>& observed) {
    const auto expected =
        std::vector<TaskState>{TaskState::Running, TaskState::Running, TaskState::Running,
                               TaskState::Running, TaskState::Completed};
    return observed == expected;
}

[[nodiscard]] Result<void> cancelledIfRequested(const TaskContext& context, std::string message) {
    if(!context.cancellation().requested()) {
        return Result<void>::success();
    }
    return Result<void>::failure(cancelledError(std::move(message)));
}

struct CopiedIntResult final {
    bool submitted{false};
    double progress{0.0};
    int value{0};
    bool named{false};
    bool removed{false};
    bool missing{false};
    int remaining{0};
    int moved_value{0};
};

[[nodiscard]] CopiedIntResult completeCopiedIntAnswer() {
    CopiedIntResult seen;
    Executor executor{1};
    TaskRegistry tasks;
    auto submitted = tasks.submit(executor, "answer",
                                  [](const TaskContext&) { return Result<int>::success(42); });
    if(!submitted) {
        return seen;
    }
    seen.submitted = true;
    const auto& handle = submitted.value();
    executor.close();
    seen.progress = handle.progress().value;
    const auto result = handle.result();
    if(!result) {
        return seen;
    }
    seen.value = result->value();
    const auto described = tasks.describe(handle.id());
    seen.named = described && described.value().name == "answer";
    auto copied = handle;
    copied = handle;
    auto moved = std::move(copied);
    seen.removed = static_cast<bool>(tasks.remove(moved.id()));
    seen.missing = !tasks.describe(handle.id());
    seen.remaining = result->value();
    const auto moved_result = moved.result();
    if(!moved_result) {
        return seen;
    }
    seen.moved_value = moved_result->value();
    return seen;
}

TEST(TaskId, ParsesOnlyCanonicalNonZeroSerials) {
    const auto valid = TaskId::parse("task:42");
    ASSERT_TRUE(valid);
    EXPECT_EQ(valid.value().str(), "task:42");
    EXPECT_EQ(valid.value(), TaskId::parse("task:42").value());
    EXPECT_NE(valid.value(), TaskId::parse("task:43").value());
    for(const auto* const invalid :
        {"", "task:", "task:0", "task:01", "Task:1", "task:-1", "task:1x"}) {
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
    const auto seen = completeCopiedIntAnswer();
    EXPECT_TRUE(seen.submitted);
    EXPECT_EQ(seen.progress, 1.0);
    EXPECT_EQ(seen.value, 42);
    EXPECT_TRUE(seen.named);
    EXPECT_TRUE(seen.removed);
    EXPECT_TRUE(seen.missing);
    EXPECT_EQ(seen.remaining, 42);
    EXPECT_EQ(seen.moved_value, 42);
}

TEST(TaskRegistry, PreservesValueResults) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto submitted = tasks.submit(executor, "value", [](const TaskContext&) {
        return Result<Value>::success(Value{Value::Object{{"answer", Value{42}}}});
    });
    ASSERT_TRUE(submitted);
    executor.close();
    const auto result = submitted.value().result();
    if(const auto* completed = peekOptional(result)) {
        EXPECT_EQ(completed->value().asObject().at("answer").asInteger(), 42);
    } else {
        ADD_FAILURE();
    }
}

TEST(TaskRegistry, PreservesResourceHandleResults) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto id = axiom::resource::ResourceId::parse("task_shape:1").value();
    const auto submitted = tasks.submit(executor, "resource", [id](const TaskContext&) {
        return Result<axiom::resource::Handle<TaskShape>>::success(
            axiom::resource::Handle<TaskShape>{id});
    });
    ASSERT_TRUE(submitted);
    executor.close();
    const auto result = submitted.value().result();
    if(const auto* completed = peekOptional(result)) {
        EXPECT_EQ(completed->value().id().str(), "task_shape:1");
    } else {
        ADD_FAILURE();
    }
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
    const auto result = submitted.value().result();
    if(const auto* completed = peekOptional(result)) {
        EXPECT_EQ(completed->value(), 42);
    } else {
        ADD_FAILURE();
    }
}

TEST(TaskRegistry, AllocatesUniqueIdentitiesAcrossRegistries) {
    Executor executor{1};
    TaskRegistry first;
    TaskRegistry second;
    const auto left =
        first.submit(executor, "left", [](const TaskContext&) { return Result<void>::success(); });
    const auto right = second.submit(executor, "right",
                                     [](const TaskContext&) { return Result<void>::success(); });
    ASSERT_TRUE(left);
    ASSERT_TRUE(right);
    EXPECT_NE(left.value().id(), right.value().id());
    executor.close();
}

TEST(TaskRegistry, ReportsFailureAndNormalizesExceptions) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto business = tasks.submit(executor, "failure", [](const TaskContext&) {
        return Result<void>::failure({.code = ErrorCode::InvalidArgument,
                                      .message = "business",
                                      .path = std::nullopt,
                                      .details = std::nullopt});
    });
    const auto exception =
        tasks.submit(executor, "exception", [](const TaskContext&) -> Result<void> {
            throw std::runtime_error{"hidden"};
        });
    ASSERT_TRUE(business);
    ASSERT_TRUE(exception);
    executor.close();
    const auto business_result = business.value().result();
    const auto exception_result = exception.value().result();
    if(const auto* failed = peekOptional(business_result)) {
        EXPECT_EQ(failed->error().code, ErrorCode::InvalidArgument);
    } else {
        ADD_FAILURE();
    }
    if(const auto* failed = peekOptional(exception_result)) {
        EXPECT_EQ(failed->error().code, ErrorCode::InvocationFailed);
    } else {
        ADD_FAILURE();
    }
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
        executor, "blocker", [&started, wait = release.get_future()](const TaskContext&) mutable {
            started.set_value();
            wait.wait();
            return Result<void>::success();
        });
    ASSERT_TRUE(blocker);
    started.get_future().wait();
    std::atomic_bool called{false};
    const auto pending = tasks.submit(executor, "pending", [&called](const TaskContext&) {
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
    const auto result = pending.value().result();
    if(const auto* completed = peekOptional(result)) {
        EXPECT_EQ(completed->error().code, ErrorCode::Cancelled);
    } else {
        ADD_FAILURE();
    }
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
        EXPECT_TRUE(reportProgressContract(context));
        return Result<void>::success();
    });
    EXPECT_TRUE(submitted);
    executor.close();
    EXPECT_EQ(submitted.value().progress().value, 1.0);
    EXPECT_EQ(submitted.value().progress().message, "rollback");
    EXPECT_TRUE(isRunningThenCompleted(observed));
    EXPECT_TRUE(subscription.active());
}

TEST(TaskRegistry, KeepsLastProgressOnFailureAndCancellation) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto failed = tasks.submit(executor, "failed-progress", [](TaskContext& context) {
        context.reportProgress(0.4, "before-fail");
        return Result<void>::failure({.code = ErrorCode::InvalidArgument,
                                      .message = "failed",
                                      .path = std::nullopt,
                                      .details = std::nullopt});
    });
    const auto cancelled = tasks.submit(executor, "cancelled-progress", [](TaskContext& context) {
        context.reportProgress(0.6, "before-cancel");
        return Result<void>::failure(cancelledError("cancelled"));
    });
    ASSERT_TRUE(failed);
    ASSERT_TRUE(cancelled);
    executor.close();
    EXPECT_EQ(failed.value().state(), TaskState::Failed);
    EXPECT_EQ(failed.value().progress().value, 0.4);
    EXPECT_EQ(failed.value().progress().message, "before-fail");
    EXPECT_EQ(cancelled.value().state(), TaskState::Cancelled);
    EXPECT_EQ(cancelled.value().progress().value, 0.6);
    EXPECT_EQ(cancelled.value().progress().message, "before-cancel");
}

TEST(TaskRegistry, RejectsProgressFromNonExecutionThread) {
    Executor executor{1};
    TaskRegistry tasks;
    std::promise<TaskContext*> context_ptr;
    std::promise<void> release;
    const auto submitted =
        tasks.submit(executor, "off-thread",
                     [&context_ptr, wait = release.get_future()](TaskContext& context) mutable {
                         context_ptr.set_value(&context);
                         wait.wait();
                         return Result<void>::success();
                     });
    ASSERT_TRUE(submitted);
    auto* const context = context_ptr.get_future().get();
    EXPECT_THROW(context->reportProgress(0.5, "off"), std::logic_error);
    release.set_value();
    executor.close();
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
    const auto submitted =
        tasks.submit(executor, "active", [&release, &running](const TaskContext&) {
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
    const auto submitted = tasks.submit(executor, "closed",
                                        [](const TaskContext&) { return Result<int>::success(1); });
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
    const auto submitted = tasks.submit(executor, "reentrant", [&](const TaskContext& context) {
        running.set_value();
        release.get_future().wait();
        return cancelledIfRequested(context, "cancelled");
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
    const auto first = tasks.submit(executor, "first", [&release, &running](const TaskContext&) {
        running.set_value();
        release.get_future().wait();
        return Result<void>::success();
    });
    ASSERT_TRUE(first);
    running.get_future().wait();
    EXPECT_FALSE(first.value().result());
    const auto second = tasks.submit(executor, "second",
                                     [](const TaskContext&) { return Result<void>::success(); });
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
        tasks.submit(executor, "cooperative", [&running, &release](const TaskContext& context) {
            running.set_value();
            release.get_future().wait();
            if(!context.cancellation().requested()) {
                return Result<int>::success(1);
            }
            return Result<int>::failure({.code = ErrorCode::Cancelled,
                                         .message = "noticed cancellation",
                                         .path = std::nullopt,
                                         .details = std::nullopt});
        });
    ASSERT_TRUE(submitted);
    running.get_future().wait();
    submitted.value().cancel();
    release.set_value();
    executor.close();
    EXPECT_EQ(submitted.value().state(), TaskState::Cancelled);
    const auto result = submitted.value().result();
    if(const auto* completed = peekOptional(result)) {
        EXPECT_TRUE(completed->hasError());
    } else {
        ADD_FAILURE();
    }
}

TEST(TaskRegistry, NormalizesUnknownExceptions) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto submitted =
        tasks.submit(executor, "unknown", [](const TaskContext&) -> Result<int> { throw 1; });
    ASSERT_TRUE(submitted);
    executor.close();
    const auto result = submitted.value().result();
    if(const auto* completed = peekOptional(result)) {
        EXPECT_EQ(completed->error().code, ErrorCode::InternalError);
    } else {
        ADD_FAILURE();
    }
}

TEST(TaskRegistry, AddsTaskIdentityToBusinessLogContext) {
    axiom::logging::LoggingService logging;
    auto collector = std::make_shared<axiom::logging::LogCollector>();
    auto sink = logging.addSink(collector);
    Executor executor{1};
    TaskRegistry tasks{logging.logger("root")};
    const auto submitted = tasks.submit(executor, "logged", [&logging](const TaskContext&) {
        AXIOM_LOG_INFO(logging.logger("business"), "business work");
        return Result<void>::success();
    });
    ASSERT_TRUE(submitted);
    executor.close();
    const auto records = collector->records();
    ASSERT_FALSE(records.empty());
    const axiom::logging::LogRecord* business = nullptr;
    for(const auto& record : records) {
        if(record.category == "business") {
            business = &record;
        }
    }
    ASSERT_NE(business, nullptr);
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
    std::optional<TaskHandle<int>> handle;
    std::vector<TaskState> observed;
    std::size_t observed_before_destruction = 0U;
    {
        TaskRegistry tasks{logging.logger("root")};
        subscription = tasks.onChanged([](const auto&) { throw std::runtime_error{"subscriber"}; });
        auto good = tasks.onChanged(
            [&observed](const auto& descriptor) { observed.push_back(descriptor.state); });
        const auto submitted =
            tasks.submit(executor, "lifetime", [&release, &running](const TaskContext&) {
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
    if(!handle) {
        FAIL();
        return;
    }
    const auto result = handle->result();
    if(const auto* completed = peekOptional(result)) {
        EXPECT_EQ(completed->value(), 7);
    } else {
        ADD_FAILURE();
    }
    EXPECT_FALSE(subscription.active());
    EXPECT_EQ(observed.size(), observed_before_destruction);
    const auto records = collector->records();
    bool saw_callback_failure = false;
    for(const auto& record : records) {
        if(record.message.find("callback failed") != std::string::npos) {
            saw_callback_failure = true;
        }
    }
    EXPECT_TRUE(saw_callback_failure);
    static_cast<void>(sink);
}

TEST(TaskRegistry, NestedProgressFromTheExecutionThreadKeepsTheLastMessage) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto submitted = tasks.submit(executor, "nested", [](TaskContext& context) {
        context.reportProgress(0.5, "nested");
        return Result<void>::success();
    });
    ASSERT_TRUE(submitted);
    executor.close();
    EXPECT_EQ(submitted.value().state(), TaskState::Completed);
    EXPECT_EQ(submitted.value().progress().value, 1.0);
    EXPECT_EQ(submitted.value().progress().message, "nested");
    submitted.value().cancel();
    EXPECT_EQ(submitted.value().state(), TaskState::Completed);
}

struct NestedControlResult final {
    bool started{false};
    bool restart_rejected{false};
    TaskState state{TaskState::Pending};
    double progress{0.0};
    std::string message;
};

[[nodiscard]] NestedControlResult exerciseNestedControlNotifications() {
    NestedControlResult seen;
    axiom::logging::LoggingService logging;
    auto collector = std::make_shared<axiom::logging::LogCollector>();
    auto sink = logging.addSink(collector);
    auto hub = std::make_shared<axiom::task::detail::NotificationHub>(logging.logger("notify"));
    auto control = std::make_shared<axiom::task::detail::TaskControl>(
        TaskId::parse("task:100000").value(), "nested", hub, logging.logger("task"), [] {
            return std::make_shared<const Result<int>>(
                Result<int>::failure(cancelledError("cancelled")));
        });
    auto subscription = hub->connect([&](const auto& descriptor) {
        if(descriptor.state == TaskState::Running && descriptor.progress.value == 0.0) {
            control->reportProgress(0.5, "nested");
        }
    });
    seen.started = control->start();
    seen.restart_rejected = !control->start();
    control->complete(TaskState::Completed,
                      std::make_shared<const Result<int>>(Result<int>::success(1)), std::nullopt);
    control->reportProgress(0.9, "late");
    control->complete(
        TaskState::Failed,
        std::make_shared<const Result<int>>(Result<int>::failure({.code = ErrorCode::InternalError,
                                                                  .message = "ignored",
                                                                  .path = std::nullopt,
                                                                  .details = std::nullopt})),
        Error{.code = ErrorCode::InternalError,
              .message = "ignored",
              .path = std::nullopt,
              .details = std::nullopt});
    seen.state = control->state();
    seen.progress = control->progress().value;
    seen.message = control->progress().message;
    static_cast<void>(subscription);
    static_cast<void>(sink);
    return seen;
}

TEST(TaskControl, SerializesNestedNotificationsAndKeepsTerminalState) {
    const auto seen = exerciseNestedControlNotifications();
    EXPECT_TRUE(seen.started);
    EXPECT_TRUE(seen.restart_rejected);
    EXPECT_EQ(seen.state, TaskState::Completed);
    EXPECT_EQ(seen.progress, 1.0);
    EXPECT_EQ(seen.message, "nested");
}
} // namespace
