#include <axiom/async/executor.hpp>
#include <axiom/async/scheduler.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;

TEST(Executor, ResolvesZeroWorkersAndReturnsResults) {
    axiom::async::Executor executor{0};
    auto result = executor.submit([](const int value) { return value * 2; }, 21);
    EXPECT_EQ(result.get(), 42);
}

TEST(Executor, SupportsMoveOnlyWorkAndReportsFailuresThroughFuture) {
    axiom::async::Executor executor{1};
    auto value = executor.submit([](std::unique_ptr<int> input) { return *input; },
                                 std::make_unique<int>(9));
    auto failure = executor.submit([]() -> int { throw std::runtime_error{"failure"}; });

    EXPECT_EQ(value.get(), 9);
    EXPECT_THROW(failure.get(), std::runtime_error);
}

TEST(Executor, CloseDrainsWorkAndRejectsLaterSubmissions) {
    axiom::async::Executor executor{1};
    std::atomic<int> completed{0};
    std::vector<std::future<void>> accepted;
    for(int index = 0; index < 3; ++index) {
        accepted.push_back(executor.submit([&completed] { ++completed; }));
    }

    executor.close();

    for(auto& future : accepted) {
        future.get();
    }
    EXPECT_EQ(completed.load(), 3);
    const auto submit_after_close = [&executor] {
        auto future = executor.submit([] {});
        static_cast<void>(future);
    };
    EXPECT_THROW(submit_after_close(), std::runtime_error);
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
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    const auto worker = executor.submit([] { return std::this_thread::get_id(); }).get();
    std::promise<std::thread::id> callback_thread;
    auto callback_future = callback_thread.get_future();
    const auto handle = scheduler.scheduleAfter(
        1ms, [&callback_thread] { callback_thread.set_value(std::this_thread::get_id()); });

    EXPECT_EQ(callback_future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(callback_future.get(), worker);
    EXPECT_FALSE(handle.active());
}

TEST(Scheduler, CancelsPeriodicCallbacks) {
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};
    std::mutex mutex;
    std::condition_variable reached;
    int calls = 0;
    auto handle = scheduler.scheduleEvery(2ms, [&] {
        std::lock_guard lock{mutex};
        ++calls;
        reached.notify_all();
    });

    {
        std::unique_lock lock{mutex};
        ASSERT_TRUE(reached.wait_for(lock, 1s, [&] { return calls >= 2; }));
    }
    EXPECT_TRUE(handle.cancel());
    const int after_cancel = [&] {
        std::lock_guard lock{mutex};
        return calls;
    }();
    std::this_thread::sleep_for(20ms);
    std::lock_guard lock{mutex};
    EXPECT_EQ(calls, after_cancel);
    EXPECT_FALSE(handle.active());
}

TEST(Scheduler, ValidatesInputsAndClosedExecutor) {
    axiom::async::Executor executor{1};
    axiom::async::Scheduler scheduler{executor};

    const auto schedule_negative = [&scheduler] {
        auto handle = scheduler.scheduleAfter(-1ms, [] {});
        static_cast<void>(handle);
    };
    const auto schedule_zero_period = [&scheduler] {
        auto handle = scheduler.scheduleEvery(0ms, [] {});
        static_cast<void>(handle);
    };
    const auto schedule_empty = [&scheduler] {
        auto handle = scheduler.scheduleAfter(1ms, {});
        static_cast<void>(handle);
    };
    EXPECT_THROW(schedule_negative(), std::invalid_argument);
    EXPECT_THROW(schedule_zero_period(), std::invalid_argument);
    EXPECT_THROW(schedule_empty(), std::invalid_argument);

    executor.close();
    const auto schedule_after_close = [&scheduler] {
        auto handle = scheduler.scheduleAfter(1ms, [] {});
        static_cast<void>(handle);
    };
    EXPECT_THROW(schedule_after_close(), std::runtime_error);
}

} // namespace
