#pragma once

/**
 * @file task_types.hpp
 * @brief Value types used by the task runtime public interface.
 */

#include <axiom/export.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/task/task_id.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace axiom::task {

class TaskContext;
namespace detail {
class TaskControl;
template <typename T, typename Function>
void execute(const std::shared_ptr<TaskControl>& control, Function& function);
} // namespace detail

/** @brief Lifecycle state of a submitted task. */
enum class TaskState : std::uint8_t { Pending, Running, Completed, Failed, Cancelled };

/** @brief A task's copied progress value and message. Progress accepts finite [0,1] values. */
struct Progress final {
    double value{0.0};
    std::string message;
    [[nodiscard]] bool operator==(const Progress&) const = default;
};

/** @brief A consistent value snapshot of one task's observable metadata. */
struct TaskDescriptor final {
    TaskId id;
    std::string name;
    TaskState state{TaskState::Pending};
    Progress progress;
    std::optional<Error> error;
};

/** @brief Copyable, thread-safe observation of a task's cooperative cancellation request. */
class AXIOM_API CancellationToken final {
public:
    /** @brief Returns whether cancellation was requested. */
    [[nodiscard]] bool requested() const noexcept;

private:
    friend class TaskRegistry;
    friend class detail::TaskControl;
    friend class TaskContext;
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> requested) noexcept
        : requested_(std::move(requested)) {}
    std::shared_ptr<std::atomic_bool> requested_;
};

/**
 * @brief Non-copyable execution capability valid only while its task function runs.
 */
class AXIOM_API TaskContext final {
public:
    TaskContext(const TaskContext&) = delete;
    TaskContext& operator=(const TaskContext&) = delete;
    TaskContext(TaskContext&&) = delete;
    TaskContext& operator=(TaskContext&&) = delete;
    ~TaskContext() = default;

    /** @brief Returns the identity of the currently executing task. */
    [[nodiscard]] const TaskId& id() const noexcept;
    /** @brief Returns the task's thread-safe cancellation observation token. */
    [[nodiscard]] CancellationToken cancellation() const noexcept;
    /**
     * @throws std::invalid_argument when value is not finite or outside [0, 1].
     * @throws std::logic_error when called from a thread other than the task's
     * execution thread.
     */
    void reportProgress(double value, std::string message = {});

private:
    friend class detail::TaskControl;
    template <typename T, typename Function>
    friend void detail::execute(const std::shared_ptr<detail::TaskControl>& /*control*/,
                                Function& /*function*/);
    explicit TaskContext(std::shared_ptr<detail::TaskControl> control) noexcept
        : control_(std::move(control)) {}
    std::shared_ptr<detail::TaskControl> control_;
};

[[nodiscard]] constexpr bool isTerminal(const TaskState state) noexcept {
    return state == TaskState::Completed || state == TaskState::Failed ||
           state == TaskState::Cancelled;
}

} // namespace axiom::task
