#pragma once

#include <axiom/events/signal.hpp>
#include <axiom/logging/logger.hpp>
#include <axiom/task/task_types.hpp>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace axiom::task::detail {

class NotificationHub final {
public:
    explicit NotificationHub(logging::Logger logger);
    using Subscription = events::Signal<const TaskDescriptor&>::Subscription;
    [[nodiscard]] Subscription connect(std::function<void(const TaskDescriptor&)> callback);
    void close() noexcept;
    void emit(const TaskDescriptor& descriptor) noexcept;

private:
    events::Signal<const TaskDescriptor&> signal_;
    logging::Logger logger_;
    std::atomic_bool closed_{false};
};

class AXIOM_API TaskControl final : public std::enable_shared_from_this<TaskControl> {
public:
    TaskControl(TaskId id,
                std::string name,
                std::weak_ptr<NotificationHub> notifications,
                logging::Logger&& logger,
                std::function<std::shared_ptr<const void>()> cancelled_result);

    [[nodiscard]] TaskDescriptor describe() const;
    [[nodiscard]] const TaskId& id() const noexcept { return descriptor_.id; }
    [[nodiscard]] TaskState state() const;
    [[nodiscard]] Progress progress() const;
    [[nodiscard]] CancellationToken cancellation() const noexcept;
    [[nodiscard]] const logging::Logger& logger() const noexcept { return logger_; }
    [[nodiscard]] bool requestCancel();
    [[nodiscard]] bool start();
    void reportProgress(double value, std::string message);
    void complete(TaskState terminal_state,
                  std::shared_ptr<const void> completed_result,
                  std::optional<Error> error);
    [[nodiscard]] std::shared_ptr<const void> result() const;

private:
    void publish(TaskDescriptor descriptor);
    void drainNotifications() noexcept;

    mutable std::mutex mutex_;
    TaskDescriptor descriptor_;
    std::shared_ptr<std::atomic_bool> cancellation_{std::make_shared<std::atomic_bool>(false)};
    std::shared_ptr<const void> result_;
    std::weak_ptr<NotificationHub> notifications_;
    logging::Logger logger_;
    std::function<std::shared_ptr<const void>()> cancelled_result_;
    std::mutex notification_mutex_;
    std::deque<TaskDescriptor> notifications_queue_;
    bool draining_{false};
};

template <typename T>
[[nodiscard]] std::optional<Result<T>> typedResult(const std::shared_ptr<TaskControl>& control) {
    const auto erased = control->result();
    if(!erased) {
        return std::nullopt;
    }
    const auto typed = std::static_pointer_cast<const Result<T>>(erased);
    return *typed;
}

template <typename T, typename Function>
void execute(const std::shared_ptr<TaskControl>& control, Function& function) noexcept {
    if(!control->start()) {
        return;
    }
    try {
        [[maybe_unused]] auto scoped_context =
            control->logger().scopedContext({{"task_id", Value{std::string{control->id().str()}}},
                                             {"task_name", Value{control->describe().name}}});
        TaskContext context{control};
        Result<T> result = std::invoke(std::move(function), context);
        TaskState terminal = TaskState::Completed;
        if(!result) {
            terminal = result.error().code == ErrorCode::Cancelled ? TaskState::Cancelled
                                                                   : TaskState::Failed;
        }
        const auto error = result ? std::optional<Error>{} : std::optional<Error>{result.error()};
        control->complete(terminal, std::make_shared<const Result<T>>(std::move(result)), error);
    } catch(const std::exception&) {
        auto error = Error{.code = ErrorCode::InvocationFailed,
                           .message = "Task invocation failed",
                           .path = std::nullopt,
                           .details = std::nullopt};
        control->complete(TaskState::Failed,
                          std::make_shared<const Result<T>>(Result<T>::failure(error)), error);
    } catch(...) {
        auto error = Error{.code = ErrorCode::InternalError,
                           .message = "Unexpected internal failure during task invocation",
                           .path = std::nullopt,
                           .details = std::nullopt};
        control->complete(TaskState::Failed,
                          std::make_shared<const Result<T>>(Result<T>::failure(error)), error);
    }
}

} // namespace axiom::task::detail
