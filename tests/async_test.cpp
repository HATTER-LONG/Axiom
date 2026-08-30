#include <axiom/async/executor.hpp>
#include <axiom/async/scheduler.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

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

void waitForAll(std::vector<std::future<void>>& futures) {
    for(auto& future : futures) {
        future.get();
    }
}

void submitThree(axiom::async::Executor& executor,
                 std::atomic<int>& completed,
                 std::vector<std::future<void>>& futures) {
    futures.reserve(3);
    for(int index = 0; index < 3; ++index) {
        futures.push_back(executor.submit([&completed] { ++completed; }));
    }
}

struct PeriodicCallCounter final {
    void operator()() {
        std::scoped_lock const lock{mutex};
        ++calls;
        reached.notify_all();
    }

    [[nodiscard]] bool waitFor(const int expected) {
        std::unique_lock lock{mutex};
        return reached.wait_for(lock, std::chrono::seconds{1},
                                [this, expected] { return calls >= expected; });
    }

    [[nodiscard]] int count() const {
        std::scoped_lock const lock{mutex};
        return calls;
    }

    mutable std::mutex mutex;
    std::condition_variable reached;
    int calls{0};
};

[[nodiscard]] int doubleValue(const int value) { return value * 2; }

[[nodiscard]] int dereferenceValue(std::unique_ptr<int> input) { return *input; }

[[nodiscard]] int failedValue() { throw std::runtime_error{"failure"}; }

struct FutureFailure final {
    void operator()() { static_cast<void>(future.get()); }

    std::future<int>& future;
};

[[nodiscard]] bool waitUntilInactive(const axiom::async::Scheduler::ScheduleHandle& handle) {
    const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    while(handle.active() && std::chrono::steady_clock::now() < timeout) {
        std::this_thread::yield();
    }
    return !handle.active();
}

struct RefQualifiedCallable final {
    int operator()() & { return 1; }
    std::string operator()() && { return "owned"; }
};

struct SetFlag final {
    void operator()() const { flag = true; }
    std::atomic<bool>& flag;
};

struct NotifyOnce final {
    void operator()() const { promise.set_value(); }
    std::promise<void>& promise;
};

struct RecordTime final {
    void operator()() const { promise.set_value(std::chrono::steady_clock::now()); }
    std::promise<std::chrono::steady_clock::time_point>& promise;
};

template <typename T> T transferOwnership(T& source) { return std::move(source); }

TEST(Executor, ResolvesZeroWorkersAndReturnsResults) {
    axiom::async::Executor executor{0};
    auto result = executor.submit(doubleValue, 21);
    EXPECT_EQ(result.get(), 42);
}

TEST(Executor, SupportsMoveOnlyWorkAndReportsFailuresThroughFuture) {
    axiom::async::Executor executor{1};
    auto value = executor.submit(dereferenceValue, std::make_unique<int>(9));
    auto failure = executor.submit(failedValue);

    EXPECT_EQ(value.get(), 9);
    EXPECT_TRUE(throws<std::runtime_error>(FutureFailure{failure}));
}

TEST(Executor, CloseDrainsWorkAndRejectsLaterSubmissions) {
    std::atomic<int> completed{0};
    axiom::async::Executor executor{1};
    std::vector<std::future<void>> accepted;
    submitThree(executor, completed, accepted);

    executor.close();

    waitForAll(accepted);
    EXPECT_EQ(completed.load(), 3);
    EXPECT_TRUE(
        throws<std::runtime_error>([&executor] { static_cast<void>(executor.submit([] {})); }));
    EXPECT_NO_THROW(executor.close());
}

TEST(Executor, RejectsCloseFromItsWorker) {
    axiom::async::Executor executor{1};
    auto result = executor.submit([&executor] {
        try {
            executor.close();
        } catch(const std::logic_error&) {
            return true;
        }
        return false;
    });

    EXPECT_TRUE(result.get());
}

TEST(Scheduler, DispatchesOneShotCallbackThroughExecutor) {
    std::promise<std::thread::id> callback_thread;
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    const auto worker = executor.submit([] { return std::this_thread::get_id(); }).get();
    auto callback_future = callback_thread.get_future();
    const auto handle = scheduler.scheduleAfter(std::chrono::milliseconds{1}, [&callback_thread] {
        callback_thread.set_value(std::this_thread::get_id());
    });

    ASSERT_EQ(callback_future.wait_for(std::chrono::seconds{1}), std::future_status::ready);
    EXPECT_EQ(callback_future.get(), worker);
    EXPECT_FALSE(handle.active());
}

TEST(Scheduler, CancelsPeriodicCallbacks) {
    PeriodicCallCounter counter;
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    auto handle = scheduler.scheduleEvery(std::chrono::milliseconds{2}, std::ref(counter));

    ASSERT_TRUE(counter.waitFor(2));
    EXPECT_TRUE(handle.cancel());
    // Cancellation cannot retract work already accepted by the executor.
    executor.close();
    EXPECT_GE(counter.count(), 2);
    EXPECT_FALSE(handle.active());
}

TEST(Scheduler, CancelsOnlyTheRequestedCallback) {
    std::promise<void> retained_callback;
    std::atomic<bool> cancelled_called{false};
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    auto retained_future = retained_callback.get_future();
    auto cancelled = scheduler.scheduleAfter(std::chrono::hours{1}, SetFlag{cancelled_called});
    const auto retained =
        scheduler.scheduleAfter(std::chrono::milliseconds{0}, NotifyOnce{retained_callback});

    EXPECT_TRUE(cancelled.cancel());
    EXPECT_FALSE(cancelled.active());
    EXPECT_EQ(retained_future.wait_for(std::chrono::seconds{1}), std::future_status::ready);
    executor.close();
    EXPECT_FALSE(cancelled_called.load());
    EXPECT_FALSE(retained.active());
}

TEST(Scheduler, MoveAssignmentDeactivatesTheMovedFromHandle) {
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    auto transferred = scheduler.scheduleAfter(std::chrono::hours{1}, [] {});
    auto owner = scheduler.scheduleAfter(std::chrono::hours{1}, [] {});

    owner = transferOwnership(transferred);
    EXPECT_FALSE(transferred.active());
    EXPECT_TRUE(owner.active());
}

TEST(Scheduler, MoveAssignmentCancelsTheReplacedCallback) {
    std::atomic<bool> replaced_called{false};
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    auto transferred = scheduler.scheduleAfter(std::chrono::hours{1}, [] {});
    auto owner = scheduler.scheduleAfter(std::chrono::hours{1}, SetFlag{replaced_called});

    owner = transferOwnership(transferred);
    EXPECT_TRUE(owner.cancel());
    executor.close();
    EXPECT_FALSE(replaced_called.load());
}

TEST(Scheduler, StopsPeriodicCallbacksWhenItsExecutorCloses) {
    PeriodicCallCounter counter;
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    auto handle = scheduler.scheduleEvery(std::chrono::milliseconds{1}, std::ref(counter));

    ASSERT_TRUE(counter.waitFor(1));
    executor.close();
    EXPECT_TRUE(waitUntilInactive(handle));
    EXPECT_GE(counter.count(), 1);
}

TEST(Scheduler, ValidatesInputsAndClosedExecutor) {
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};

    EXPECT_TRUE(throws<std::invalid_argument>([&scheduler] {
        static_cast<void>(scheduler.scheduleAfter(-std::chrono::milliseconds{1}, [] {}));
    }));
    EXPECT_TRUE(throws<std::invalid_argument>([&scheduler] {
        static_cast<void>(scheduler.scheduleEvery(std::chrono::milliseconds{0}, [] {}));
    }));
    EXPECT_TRUE(throws<std::invalid_argument>([&scheduler] {
        static_cast<void>(scheduler.scheduleAfter(std::chrono::milliseconds{1}, {}));
    }));

    executor.close();
    EXPECT_TRUE(throws<std::runtime_error>([&scheduler] {
        static_cast<void>(scheduler.scheduleAfter(std::chrono::milliseconds{1}, [] {}));
    }));
}

TEST(Executor, InfersResultFromTheOwnedCallableAndArguments) {
    RefQualifiedCallable callable;
    int value = 4;
    axiom::async::Executor executor{1};
    auto owned = executor.submit(callable);
    auto referenced = executor.submit(std::ref(callable));
    auto mutation = executor.submit([](int& target) { ++target; }, std::ref(value));

    EXPECT_EQ(owned.get(), "owned");
    EXPECT_EQ(referenced.get(), 1);
    mutation.get();
    EXPECT_EQ(value, 5);
}

TEST(Scheduler, CancelledEarlierDeadlineDoesNotAdvanceTheNextCallback) {
    std::promise<std::chrono::steady_clock::time_point> called;
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    auto future = called.get_future();
    auto earlier = scheduler.scheduleAfter(std::chrono::milliseconds{20}, [] {});
    const auto earliest_allowed = std::chrono::steady_clock::now() + std::chrono::milliseconds{100};
    [[maybe_unused]] const auto later =
        scheduler.scheduleAfter(std::chrono::milliseconds{100}, RecordTime{called});
    EXPECT_TRUE(earlier.cancel());

    ASSERT_EQ(future.wait_for(std::chrono::seconds{1}), std::future_status::ready);
    EXPECT_GE(future.get(), earliest_allowed);
}

TEST(Scheduler, LaterCallbackHandleIsInactiveAfterDispatch) {
    std::promise<std::chrono::steady_clock::time_point> called;
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    auto future = called.get_future();
    auto earlier = scheduler.scheduleAfter(std::chrono::milliseconds{20}, [] {});
    const auto later = scheduler.scheduleAfter(std::chrono::milliseconds{100}, RecordTime{called});
    EXPECT_TRUE(earlier.cancel());

    ASSERT_EQ(future.wait_for(std::chrono::seconds{1}), std::future_status::ready);
    EXPECT_FALSE(later.active());
}

TEST(Scheduler, ReleasesCapturedHandlesOutsideTheSchedulerLock) {
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    auto inner = std::make_shared<axiom::async::Scheduler::ScheduleHandle>(
        scheduler.scheduleAfter(std::chrono::hours{1}, [] {}));
    const std::weak_ptr<axiom::async::Scheduler::ScheduleHandle> weak = inner;
    auto outer = scheduler.scheduleAfter(std::chrono::hours{1}, [inner] {});
    inner = nullptr;

    EXPECT_TRUE(outer.cancel());
    EXPECT_TRUE(weak.expired());
}

TEST(Scheduler, KeepsMutableCallbackStateBetweenPeriodicDispatches) {
    std::promise<int> reached;
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    auto future = reached.get_future();
    auto handle =
        scheduler.scheduleEvery(std::chrono::milliseconds{1}, [count = 0, &reached]() mutable {
            if(++count == 2) {
                reached.set_value(count);
            }
        });

    ASSERT_EQ(future.wait_for(std::chrono::seconds{1}), std::future_status::ready);
    EXPECT_EQ(future.get(), 2);
    EXPECT_TRUE(handle.cancel());
    executor.close();
}

} // namespace
