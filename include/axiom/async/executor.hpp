#pragma once

/**
 * @file executor.hpp
 * @brief A draining, fixed-size asynchronous task executor.
 */

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace axiom::async {

class Scheduler;

/**
 * @brief Runs submitted work on a fixed set of worker threads.
 *
 * close() prevents new submissions, drains accepted work, and joins workers.
 * It must not be called from one of this executor's workers. Destruction performs
 * the same shutdown and therefore must occur outside an executor task.
 */
class Executor final {
public:
    /**
     * @brief Starts worker threads.
     * @param worker_count Number of workers; zero creates one worker.
     * @throws std::system_error if a worker thread cannot be started.
     */
    explicit Executor(std::size_t worker_count = std::thread::hardware_concurrency());

    /** @brief Drains accepted work and joins all workers. */
    ~Executor() noexcept;

    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
    Executor(Executor&&) = delete;
    Executor& operator=(Executor&&) = delete;

    /**
     * @brief Submits a callable for asynchronous execution.
     * @tparam F Callable type.
     * @tparam Args Argument types.
     * @param function Callable to invoke.
     * @param args Arguments captured for the invocation.
     * @return Future for the callable result or exception.
     * @throws std::runtime_error if close() has begun.
     */
    template <typename F, typename... Args>
    [[nodiscard]] auto submit(F&& function, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    /**
     * @brief Stops accepting work, drains queued work, and joins workers.
     *
     * This operation is idempotent. Calling it from this executor's worker
     * throws std::logic_error because a thread cannot join itself.
     *
     * @throws std::logic_error if called by one of this executor's workers.
     */
    void close();

private:
    friend class Scheduler;

    void enqueue(std::function<void()> task);
    [[nodiscard]] bool accepting() const noexcept;

    struct State;
    std::unique_ptr<State> state_;
};

template <typename F, typename... Args>
auto Executor::submit(F&& function, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
    using Result = std::invoke_result_t<F, Args...>;
    using Function = std::decay_t<F>;
    using Arguments = std::tuple<std::decay_t<Args>...>;

    auto task = std::make_shared<std::packaged_task<Result()>>(
        [function = Function(std::forward<F>(function)),
         arguments = Arguments(std::forward<Args>(args)...)]() mutable -> Result {
            return std::apply(
                [&function](auto&&... unpacked) -> Result {
                    return std::invoke(std::move(function), std::move(unpacked)...);
                },
                std::move(arguments));
        });
    auto future = task->get_future();
    enqueue([task] { (*task)(); });
    return future;
}

} // namespace axiom::async
