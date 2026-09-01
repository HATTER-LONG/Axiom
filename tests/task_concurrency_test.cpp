#include <axiom/async/executor.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/logging/log_collector.hpp>
#include <axiom/logging/log_record.hpp>
#include <axiom/logging/logger.hpp>
#include <axiom/logging/logging_service.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_registry.hpp>
#include <axiom/task/task_types.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <iterator>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using axiom::ErrorCode;
using axiom::Result;
using axiom::async::Executor;
using axiom::task::CancellationToken;
using axiom::task::TaskContext;
using axiom::task::TaskDescriptor;
using axiom::task::TaskOrigin;
using axiom::task::TaskRegistry;
using axiom::task::TaskState;
using axiom::task::TaskSubmission;

constexpr auto k_wait = std::chrono::seconds{2};

class Gate final {
public:
    void arrive() {
        std::scoped_lock const lock{mutex_};
        arrived_ = true;
        condition_.notify_all();
    }

    [[nodiscard]] bool waitForArrival() {
        std::unique_lock lock{mutex_};
        return condition_.wait_for(lock, k_wait, [this] { return arrived_; });
    }

    void release() {
        std::scoped_lock const lock{mutex_};
        released_ = true;
        condition_.notify_all();
    }

    void waitForRelease() {
        std::unique_lock lock{mutex_};
        condition_.wait(lock, [this] { return released_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool arrived_{false};
    bool released_{false};
};

[[nodiscard]] axiom::Error cancelledError(std::string message) {
    return {.code = ErrorCode::Cancelled,
            .message = std::move(message),
            .path = std::nullopt,
            .details = std::nullopt};
}

[[nodiscard]] Result<void> cancelledIfRequested(const TaskContext& context, std::string message) {
    if(!context.cancellation().requested()) {
        return Result<void>::success();
    }
    return Result<void>::failure(cancelledError(std::move(message)));
}

template <typename T> [[nodiscard]] const T* peekOptional(const std::optional<T>& value) {
    if(!value.has_value()) {
        return nullptr;
    }
    return std::addressof(*value);
}

[[nodiscard]] std::vector<TaskState> statesFor(const std::vector<TaskDescriptor>& observed,
                                               const axiom::task::TaskId& id) {
    std::vector<TaskState> states;
    for(const auto& descriptor : observed) {
        if(descriptor.id == id) {
            states.push_back(descriptor.state);
        }
    }
    return states;
}

[[nodiscard]] std::size_t countCompleted(const std::vector<TaskDescriptor>& observed,
                                         const axiom::task::TaskId& id) {
    return static_cast<std::size_t>(std::ranges::count_if(observed, [&](const auto& descriptor) {
        return descriptor.id == id && descriptor.state == TaskState::Completed;
    }));
}

[[nodiscard]] const axiom::logging::LogRecord*
findRecord(const std::vector<axiom::logging::LogRecord>& records, std::string_view message) {
    const auto found = std::ranges::find_if(
        records, [&](const auto& record) { return record.message == message; });
    if(found == records.end()) {
        return nullptr;
    }
    return &*found;
}

[[nodiscard]] bool
queryHandle(TaskRegistry& tasks, const axiom::task::TaskId& id, std::latch& queries_ready);

[[nodiscard]] std::vector<std::thread>
launchQueryThreads(TaskRegistry& tasks,
                   const std::vector<axiom::task::TaskHandle<void>>& handles,
                   std::latch& queries_ready,
                   std::atomic_bool& queries_ok) {
    std::vector<std::thread> readers;
    readers.reserve(handles.size());
    std::ranges::transform(handles, std::back_inserter(readers),
                           [&tasks, &queries_ready, &queries_ok](const auto& handle) {
                               return std::thread{
                                   [&tasks, &queries_ready, &queries_ok, id = handle.id()] {
                                       if(!queryHandle(tasks, id, queries_ready)) {
                                           queries_ok.store(false);
                                       }
                                   }};
                           });
    return readers;
}

[[nodiscard]] bool
queryHandle(TaskRegistry& tasks, const axiom::task::TaskId& id, std::latch& queries_ready) {
    const auto descriptor = tasks.describe(id);
    const auto list = tasks.list();
    const auto cancelled = tasks.cancel(id);
    const bool ok = static_cast<bool>(descriptor) && descriptor.value().id == id &&
                    list.size() == 3U && static_cast<bool>(cancelled);
    queries_ready.count_down();
    return ok;
}

struct ResultCopyObserver final {
    TaskRegistry* registry{nullptr};
    std::optional<axiom::task::TaskId> id;
    std::atomic_bool armed{false};
    std::atomic_uint copies{0};
};

class ReentrantResultValue final {
public:
    explicit ReentrantResultValue(std::shared_ptr<ResultCopyObserver> observer)
        : observer_(std::move(observer)) {}

    ReentrantResultValue(const ReentrantResultValue& other) : observer_(other.observer_) {
        if(observer_->armed.load(std::memory_order_acquire)) {
            if(!observer_->id.has_value()) {
                return;
            }
            EXPECT_TRUE(observer_->registry->describe(observer_->id.value()));
            ++observer_->copies;
        }
    }
    ReentrantResultValue(ReentrantResultValue&&) noexcept = default;
    ReentrantResultValue& operator=(const ReentrantResultValue&) = default;
    ReentrantResultValue& operator=(ReentrantResultValue&&) noexcept = default;
    ~ReentrantResultValue() = default;

private:
    std::shared_ptr<ResultCopyObserver> observer_;
};

TEST(TaskRegistryConcurrency, ResultCopyCanReenterRegistryQueries) {
    Executor executor{1};
    TaskRegistry tasks;
    const auto observer = std::make_shared<ResultCopyObserver>();
    observer->registry = &tasks;
    const auto submitted = tasks.submit(executor, "copy reentry", [observer](TaskContext&) {
        return Result<ReentrantResultValue>::success(ReentrantResultValue{observer});
    });
    ASSERT_TRUE(submitted);
    executor.close();
    observer->id.emplace(submitted.value().id());
    observer->armed.store(true, std::memory_order_release);

    const auto copied = submitted.value().result();
    ASSERT_TRUE(copied);
    EXPECT_GT(observer->copies.load(), 0U);
}

TEST(TaskRegistryConcurrency, PublishesDifferentTasksConcurrentlyAndEachTaskInOrder) {
    Executor executor{2};
    TaskRegistry tasks;
    std::mutex observed_mutex;
    std::vector<TaskDescriptor> observed;
    std::latch running_callbacks{2};
    Gate release_callbacks;
    auto subscription = tasks.onChanged([&](const TaskDescriptor& descriptor) {
        {
            std::scoped_lock const lock{observed_mutex};
            observed.push_back(descriptor);
        }
        if(descriptor.state == TaskState::Running && descriptor.progress.value == 0.0) {
            running_callbacks.count_down();
            release_callbacks.waitForRelease();
        }
    });

    const auto first = tasks.submit(executor, "first", [](TaskContext& context) {
        context.reportProgress(0.5, "half");
        return Result<void>::success();
    });
    const auto second = tasks.submit(executor, "second", [](TaskContext& context) {
        context.reportProgress(0.5, "half");
        return Result<void>::success();
    });
    if(!first || !second) {
        release_callbacks.release();
        executor.close();
        FAIL() << "Both tasks must be accepted";
        return;
    }

    const auto callbacks_ready = std::async(std::launch::async, [&] { running_callbacks.wait(); });
    const bool both_running = callbacks_ready.wait_for(k_wait) == std::future_status::ready;
    release_callbacks.release();
    EXPECT_TRUE(both_running);
    executor.close();

    std::vector<TaskDescriptor> snapshot;
    {
        std::scoped_lock const lock{observed_mutex};
        snapshot = observed;
    }
    const auto first_states = statesFor(snapshot, first.value().id());
    const auto second_states = statesFor(snapshot, second.value().id());
    EXPECT_EQ(first_states, (std::vector<TaskState>{TaskState::Running, TaskState::Running,
                                                    TaskState::Completed}));
    EXPECT_EQ(second_states, (std::vector<TaskState>{TaskState::Running, TaskState::Running,
                                                     TaskState::Completed}));
    static_cast<void>(subscription);
}

TEST(TaskRegistryConcurrency, CallbackCanQueryCancelAndResetItsSubscription) {
    Executor executor{1};
    TaskRegistry tasks;
    std::optional<TaskRegistry::Subscription> subscription;
    std::atomic_uint callbacks{0};
    std::promise<bool> queried;
    auto queried_future = queried.get_future();
    subscription.emplace(tasks.onChanged([&](const TaskDescriptor& descriptor) {
        ++callbacks;
        if(descriptor.state == TaskState::Running) {
            queried.set_value(tasks.describe(descriptor.id).value().state == TaskState::Running);
            EXPECT_TRUE(tasks.cancel(descriptor.id));
            EXPECT_TRUE(subscription->reset());
        }
    }));
    const auto submitted = tasks.submit(executor, "reentrant", [](const TaskContext& context) {
        return cancelledIfRequested(context, "cancelled by observer");
    });
    ASSERT_TRUE(submitted);
    EXPECT_EQ(queried_future.wait_for(k_wait), std::future_status::ready);
    if(queried_future.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
        EXPECT_TRUE(queried_future.get());
    }
    executor.close();
    EXPECT_EQ(submitted.value().state(), TaskState::Cancelled);
    EXPECT_FALSE(subscription->active());
    EXPECT_EQ(callbacks.load(), 1U);
}

TEST(TaskRegistryConcurrency, RunningCancellationDoesNotOverrideSuccessfulResultOrToken) {
    Executor executor{1};
    TaskRegistry tasks;
    Gate gate;
    std::optional<CancellationToken> token;
    const auto submitted =
        tasks.submit(executor, "ignores cancellation", [&](TaskContext& context) {
            token = context.cancellation();
            gate.arrive();
            gate.waitForRelease();
            return Result<int>::success(9);
        });
    ASSERT_TRUE(submitted);
    const bool started = gate.waitForArrival();
    ASSERT_TRUE(started);
    if(!token) {
        FAIL() << "Running task must publish its cancellation token";
        return;
    }
    EXPECT_FALSE(token->requested());
    EXPECT_TRUE(tasks.cancel(submitted.value().id()));
    EXPECT_TRUE(token->requested());
    gate.release();
    executor.close();

    EXPECT_TRUE(token->requested());
    EXPECT_EQ(submitted.value().state(), TaskState::Completed);
    const auto result = submitted.value().result();
    if(const auto* completed = peekOptional(result)) {
        EXPECT_EQ(completed->value(), 9);
    } else {
        ADD_FAILURE();
    }
    EXPECT_TRUE(tasks.cancel(submitted.value().id()));
    EXPECT_TRUE(token->requested());
}

struct ReentrantValue final {
    TaskRegistry* registry{nullptr};
    std::atomic_bool* armed{nullptr};
    std::atomic_uint* destructed{nullptr};

    ReentrantValue() = default;
    ReentrantValue(TaskRegistry& owner, std::atomic_bool& should_observe, std::atomic_uint& count)
        : registry(&owner), armed(&should_observe), destructed(&count) {}
    ReentrantValue(const ReentrantValue&) = default;
    ReentrantValue(ReentrantValue&&) noexcept = default;
    ReentrantValue& operator=(const ReentrantValue&) = default;
    ReentrantValue& operator=(ReentrantValue&&) noexcept = default;
    ~ReentrantValue() {
        if(registry != nullptr && armed != nullptr && armed->load() && destructed != nullptr) {
            static_cast<void>(registry->list());
            ++*destructed;
        }
    }
};

TEST(TaskRegistryConcurrency, TerminalRemovalReleasesResultOutsideRegistryLock) {
    Executor executor{1};
    TaskRegistry tasks;
    std::atomic_bool armed{false};
    std::atomic_uint destructed{0};
    std::optional<axiom::task::TaskId> id;
    {
        const auto submitted = tasks.submit(executor, "destructor", [&](TaskContext&) {
            return Result<ReentrantValue>::success(ReentrantValue{tasks, armed, destructed});
        });
        ASSERT_TRUE(submitted);
        id.emplace(submitted.value().id());
        executor.close();
    }
    armed.store(true);
    ASSERT_TRUE(id);
    EXPECT_TRUE(tasks.remove(*id));
    EXPECT_GT(destructed.load(), 0U);
}

TEST(TaskRegistryConcurrency, RestoresLogContextBetweenTasksOnTheSameWorker) {
    axiom::logging::LoggingService logging;
    const auto collector = std::make_shared<axiom::logging::LogCollector>();
    const auto sink = logging.addSink(collector);
    Executor executor{1};
    TaskRegistry tasks{logging.logger("root")};
    TaskOrigin first_origin{.request_id = "req-a",
                            .trace_id = {},
                            .caller = {},
                            .action_id = "alpha.one",
                            .metadata = {}};
    TaskOrigin second_origin{.request_id = "req-b",
                             .trace_id = {},
                             .caller = {},
                             .action_id = "beta.two",
                             .metadata = {}};
    const auto first =
        tasks.submit(executor, TaskSubmission{.name = "one", .origin = first_origin},
                     [&](TaskContext&) {
                         AXIOM_LOG_INFO(logging.logger("business"), "first business record");
                         return Result<void>::success();
                     });
    const auto second =
        tasks.submit(executor, TaskSubmission{.name = "two", .origin = second_origin},
                     [&](TaskContext&) {
                         AXIOM_LOG_INFO(logging.logger("business"), "second business record");
                         return Result<void>::success();
                     });
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    executor.close();

    const auto records = collector->records();
    const auto* const first_record = findRecord(records, "first business record");
    const auto* const second_record = findRecord(records, "second business record");
    ASSERT_NE(first_record, nullptr);
    ASSERT_NE(second_record, nullptr);
    EXPECT_EQ(first_record->fields.at("task_name").asString(), "one");
    EXPECT_EQ(second_record->fields.at("task_name").asString(), "two");
    EXPECT_EQ(first_record->fields.at("request_id").asString(), "req-a");
    EXPECT_EQ(second_record->fields.at("request_id").asString(), "req-b");
    EXPECT_EQ(first_record->fields.at("action").asString(), "alpha.one");
    EXPECT_EQ(second_record->fields.at("action").asString(), "beta.two");
    EXPECT_NE(first_record->fields.at("task_id").asString(),
              second_record->fields.at("task_id").asString());
    static_cast<void>(sink);
}

TEST(TaskRegistryConcurrency, KeepsTasksSafeWhenTheirLoggingServiceIsDestroyedFirst) {
    Executor executor{1};
    std::optional<TaskRegistry> tasks;
    {
        const axiom::logging::LoggingService logging;
        tasks.emplace(logging.logger("root"));
    }
    const auto submitted = tasks->submit(executor, "orphaned logger",
                                         [](TaskContext&) { return Result<int>::success(17); });
    ASSERT_TRUE(submitted);
    executor.close();
    const auto result = submitted.value().result();
    if(const auto* completed = peekOptional(result)) {
        EXPECT_EQ(completed->value(), 17);
    } else {
        ADD_FAILURE();
    }
}

TEST(TaskRegistryConcurrency, RegistryDestructionStopsFutureNotificationsWithoutWaiting) {
    Executor executor{2};
    auto tasks = std::make_unique<TaskRegistry>();
    Gate callback_gate;
    Gate second_task_gate;
    std::mutex observed_mutex;
    std::vector<TaskDescriptor> observed;
    auto subscription = tasks->onChanged([&](const TaskDescriptor& descriptor) {
        {
            std::scoped_lock const lock{observed_mutex};
            observed.push_back(descriptor);
        }
        if(descriptor.name == "first" && descriptor.state == TaskState::Running) {
            callback_gate.arrive();
            callback_gate.waitForRelease();
        }
    });
    const auto first =
        tasks->submit(executor, "first", [](TaskContext&) { return Result<void>::success(); });
    ASSERT_TRUE(first);
    ASSERT_TRUE(callback_gate.waitForArrival());
    const auto second = tasks->submit(executor, "second", [&](const TaskContext&) {
        second_task_gate.arrive();
        second_task_gate.waitForRelease();
        return Result<void>::success();
    });
    if(!second) {
        callback_gate.release();
        executor.close();
        FAIL() << "Second task must be accepted";
        return;
    }
    const bool second_started = second_task_gate.waitForArrival();

    auto destroyed = std::async(std::launch::async,
                                [registry = std::move(tasks)]() mutable { registry.reset(); });
    const bool did_not_wait = destroyed.wait_for(k_wait) == std::future_status::ready;
    second_task_gate.release();
    callback_gate.release();
    destroyed.get();
    executor.close();

    EXPECT_TRUE(second_started);
    EXPECT_TRUE(did_not_wait);
    std::size_t second_terminal_notifications = 0U;
    {
        std::scoped_lock const lock{observed_mutex};
        second_terminal_notifications = countCompleted(observed, second.value().id());
    }
    EXPECT_EQ(second_terminal_notifications, 0U);
    static_cast<void>(subscription);
}

TEST(TaskRegistryConcurrency, SupportsConcurrentSnapshotQueriesAndCancellation) {
    Executor executor{3};
    TaskRegistry tasks;
    Gate gate;
    std::latch started{3};
    std::vector<axiom::task::TaskHandle<void>> handles;
    for(const char* const name : {"alpha", "beta", "gamma"}) {
        const auto submitted = tasks.submit(executor, name, [&](TaskContext&) {
            started.count_down();
            gate.arrive();
            gate.waitForRelease();
            return Result<void>::success();
        });
        if(!submitted) {
            gate.release();
            executor.close();
            FAIL() << "Task must be accepted";
            return;
        }
        handles.push_back(submitted.value());
    }
    const auto all_started = std::async(std::launch::async, [&] { started.wait(); });
    const bool running = all_started.wait_for(k_wait) == std::future_status::ready;
    std::latch queries_ready{3};
    std::atomic_bool queries_ok{true};
    auto readers = launchQueryThreads(tasks, handles, queries_ready, queries_ok);
    const auto all_queries = std::async(std::launch::async, [&] { queries_ready.wait(); });
    const bool queries_completed = all_queries.wait_for(k_wait) == std::future_status::ready;
    gate.release();
    for(auto& reader : readers) {
        reader.join();
    }
    EXPECT_TRUE(running);
    EXPECT_TRUE(queries_completed);
    EXPECT_TRUE(queries_ok.load());
    executor.close();
    for(const auto& handle : handles) {
        EXPECT_EQ(handle.state(), TaskState::Completed);
        EXPECT_TRUE(handle.result());
    }
}

} // namespace
