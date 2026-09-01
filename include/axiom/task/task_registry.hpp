#pragma once

/**
 * @file task_registry.hpp
 * @brief Submission, observation, and lifecycle ownership for asynchronous tasks.
 */

#include <axiom/async/executor.hpp>
#include <axiom/export.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/logging/logger.hpp>
#include <axiom/task/detail/task_control.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_types.hpp>

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace axiom::task {

namespace detail {
template <typename> struct TaskResultValue;
template <typename T> struct TaskResultValue<Result<T>> {
    using ValueType = T;
};
template <typename R> using TaskResultValueT = TaskResultValue<R>::ValueType;
} // namespace detail

/** @brief Copyable observation handle that never waits or owns Registry membership.
 * @tparam T
 * Copyable task result value type, or void.
 */
template <typename T> class TaskHandle final {
public:
    TaskHandle() = delete;
    TaskHandle(const TaskHandle&) = default;
    TaskHandle(TaskHandle&&) noexcept = default;
    TaskHandle& operator=(const TaskHandle&) = default;
    TaskHandle& operator=(TaskHandle&&) noexcept = default;
    ~TaskHandle() = default;

    /** @brief Returns this task's immutable identity. */
    [[nodiscard]] TaskId id() const { return control_->describe().id; }
    /** @brief Returns a current state snapshot without waiting. */
    [[nodiscard]] TaskState state() const { return control_->state(); }
    /** @brief Returns a current progress snapshot without waiting. */
    [[nodiscard]] Progress progress() const { return control_->progress(); }
    /** @brief Requests cooperative cancellation; terminal tasks are unchanged. */
    void cancel() const { static_cast<void>(control_->requestCancel()); }
    /** @brief Returns an independent completed result, or empty while active. */
    [[nodiscard]] std::optional<Result<T>> result() const {
        return detail::typedResult<T>(control_);
    }

private:
    friend class TaskRegistry;
    explicit TaskHandle(std::shared_ptr<detail::TaskControl> control)
        : control_(std::move(control)) {}
    std::shared_ptr<detail::TaskControl> control_;
};

class AXIOM_API TaskRegistry final {
public:
    using Subscription = detail::NotificationHub::Subscription;
    /** @brief Creates a Registry with no-op diagnostic logging. */
    TaskRegistry();
    /** @brief Creates a Registry whose task diagnostics use @p logger's service. */
    explicit TaskRegistry(logging::Logger logger);
    /** @brief Releases Registry membership without cancelling accepted tasks. */
    ~TaskRegistry() noexcept;
    TaskRegistry(const TaskRegistry&) = delete;
    TaskRegistry& operator=(const TaskRegistry&) = delete;
    TaskRegistry(TaskRegistry&&) = delete;
    TaskRegistry& operator=(TaskRegistry&&) = delete;

    /** @brief Accepts a move-capable callable taking TaskContext& and returning Result<T>.
     *
     * @tparam F Callable type.
     * @param executor Executor that accepts the work.
     * @param name Owned diagnostic task name.
     * @param function Task implementation.
     * @return A copied handle or an Executor rejection error.
     * @note Delegates to the submission-value overload with unknown origin.
     */
    template <typename F>
    [[nodiscard]] auto submit(async::Executor& executor, std::string name, F&& function) -> Result<
        TaskHandle<detail::TaskResultValueT<std::invoke_result_t<std::decay_t<F>, TaskContext&>>>>;

    /**
     * @brief Accepts a Task with a submission-time name and optional origin.
     *
     * @tparam F Callable type.
     * @param executor Executor that accepts the work.
     * @param submission Owned name and optional origin copied at accept time.
     * @param function Task implementation.
     * @return A copied handle or an Executor rejection error.
     * @note Origin is immutable for this Task. Retries and derived Tasks do not inherit it.
     */
    template <typename F>
    [[nodiscard]] auto submit(async::Executor& executor, TaskSubmission submission, F&& function)
        -> Result<
            TaskHandle<detail::TaskResultValueT<std::invoke_result_t<std::decay_t<F>, TaskContext&>>>>;

    /** @brief Returns one consistent descriptor, or NotFound. */
    [[nodiscard]] Result<TaskDescriptor> describe(const TaskId& id) const;
    /** @brief Lists independent descriptors in canonical ID text order. */
    [[nodiscard]] std::vector<TaskDescriptor> list() const;
    /** @brief Requests cancellation for a known task, or returns NotFound. */
    [[nodiscard]] Result<void> cancel(const TaskId& id);
    /** @brief Removes a known terminal task, or returns InvalidArgument/NotFound. */
    [[nodiscard]] Result<void> remove(const TaskId& id);
    /** @brief Subscribes to future Running, progress, and terminal descriptor snapshots. */
    [[nodiscard]] Subscription onChanged(std::function<void(const TaskDescriptor&)> callback);

private:
    struct Impl;
    [[nodiscard]] Result<std::shared_ptr<detail::TaskControl>>
    submitControl(async::Executor& executor,
                  TaskSubmission submission,
                  std::function<void(const std::shared_ptr<detail::TaskControl>&)> function,
                  std::function<std::shared_ptr<const void>()> cancelled_result);
    std::shared_ptr<Impl> impl_;
};

template <typename F>
auto TaskRegistry::submit(async::Executor& executor, std::string name, F&& function) -> Result<
    TaskHandle<detail::TaskResultValueT<std::invoke_result_t<std::decay_t<F>, TaskContext&>>>> {
    return submit(executor, TaskSubmission{.name = std::move(name), .origin = std::nullopt},
                  std::forward<F>(function));
}

template <typename F>
auto TaskRegistry::submit(async::Executor& executor, TaskSubmission submission, F&& function)
    -> Result<
        TaskHandle<detail::TaskResultValueT<std::invoke_result_t<std::decay_t<F>, TaskContext&>>>> {
    using Returned = std::invoke_result_t<std::decay_t<F>, TaskContext&>;
    using T = detail::TaskResultValueT<Returned>;
    static_assert(std::copy_constructible<T> || std::same_as<T, void>,
                  "Task result values must be copy constructible");
    auto owned = std::make_shared<std::decay_t<F>>(std::forward<F>(function));
    auto submitted = submitControl(
        executor, std::move(submission),
        [owned](const auto& control) mutable { detail::execute<T>(control, *owned); },
        [] {
            return std::make_shared<const Result<T>>(
                Result<T>::failure({.code = ErrorCode::Cancelled,
                                    .message = "Task was cancelled before execution",
                                    .path = std::nullopt,
                                    .details = std::nullopt}));
        });
    if(!submitted) {
        return Result<TaskHandle<T>>::failure(submitted.error());
    }
    return Result<TaskHandle<T>>::success(TaskHandle<T>{std::move(submitted.value())});
}

} // namespace axiom::task
