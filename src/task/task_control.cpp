#include <axiom/task/detail/task_control.hpp>

#include <axiom/foundation/error.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/logging/log_level.hpp>
#include <axiom/logging/logger.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_types.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace axiom::task::detail {
namespace {
void diagnoseNotification(const logging::Logger& logger,
                          const TaskDescriptor& descriptor,
                          const char* message) noexcept {
    AXIOM_LOG(logger, logging::LogLevel::Error,
              (Value::Object{{"task_id", Value{std::string{descriptor.id.str()}}},
                             {"task_name", Value{descriptor.name}}}),
              "{}", message);
}

[[nodiscard]] bool isRuntimeOwnedLogField(const std::string_view key) noexcept {
    return key == "request_id" || key == "trace_id" || key == "caller" || key == "module" ||
           key == "action" || key == "task_id" || key == "task_name" || key == "status" ||
           key == "duration_ms";
}

[[nodiscard]] bool isCanonicalActionComponent(const std::string_view text) noexcept {
    return !text.empty() && std::ranges::all_of(text, [](const char character) noexcept {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '_';
    });
}

[[nodiscard]] std::optional<std::string>
moduleFromCanonicalActionId(const std::string_view action_id) {
    const auto separator = action_id.find('.');
    if(separator == std::string_view::npos || separator != action_id.rfind('.') || separator == 0U ||
       separator + 1U == action_id.size()) {
        return std::nullopt;
    }
    const auto module = action_id.substr(0U, separator);
    const auto action = action_id.substr(separator + 1U);
    if(!isCanonicalActionComponent(module) || !isCanonicalActionComponent(action)) {
        return std::nullopt;
    }
    return std::string{module};
}

void appendOriginLogFields(Value::Object& fields, const TaskOrigin& origin) {
    for(const auto& [key, value] : origin.metadata) {
        if(!isRuntimeOwnedLogField(key)) {
            fields.insert_or_assign(key, Value{value});
        }
    }
    if(!origin.request_id.empty()) {
        fields.insert_or_assign("request_id", Value{origin.request_id});
    }
    if(!origin.trace_id.empty()) {
        fields.insert_or_assign("trace_id", Value{origin.trace_id});
    }
    if(!origin.caller.empty()) {
        fields.insert_or_assign("caller", Value{origin.caller});
    }
    if(!origin.action_id.empty()) {
        fields.insert_or_assign("action", Value{origin.action_id});
        if(auto module = moduleFromCanonicalActionId(origin.action_id)) {
            fields.insert_or_assign("module", Value{std::move(*module)});
        }
    }
}
} // namespace

Value::Object taskLogFields(const TaskId& id,
                            const std::string_view name,
                            const std::optional<TaskOrigin>& origin) {
    Value::Object fields;
    if(origin) {
        appendOriginLogFields(fields, *origin);
    }
    fields.insert_or_assign("task_id", Value{std::string{id.str()}});
    fields.insert_or_assign("task_name", Value{std::string{name}});
    return fields;
}

NotificationHub::NotificationHub(logging::Logger logger) : logger_(std::move(logger)) {}

NotificationHub::Subscription
NotificationHub::connect(std::function<void(const TaskDescriptor&)> callback) {
    if(!callback) {
        throw std::invalid_argument{"Task change callback must not be empty"};
    }
    return signal_.connect(
        [callback = std::move(callback), logger = logger_](const TaskDescriptor& descriptor) {
            try {
                callback(descriptor);
            } catch(...) {
                diagnoseNotification(logger, descriptor, "task change callback failed");
            }
        });
}

void NotificationHub::emit(const TaskDescriptor& descriptor) noexcept {
    if(closed_.load(std::memory_order_acquire)) {
        return;
    }
    try {
        signal_.emit(descriptor);
    } catch(...) {
        diagnoseNotification(logger_, descriptor, "task change notification failed");
    }
}

void NotificationHub::close() noexcept { closed_.store(true, std::memory_order_release); }

TaskControl::TaskControl(TaskId id,
                         std::string name,
                         std::weak_ptr<NotificationHub> notifications,
                         logging::Logger&& logger,
                         std::function<std::shared_ptr<const void>()> cancelled_result,
                         std::optional<TaskOrigin> origin)
    : descriptor_{.id = std::move(id),
                  .name = std::move(name),
                  .state = TaskState::Pending,
                  .progress = {},
                  .error = std::nullopt,
                  .origin = std::move(origin)},
      notifications_(std::move(notifications)), logger_(std::move(logger)),
      cancelled_result_(std::move(cancelled_result)) {}

TaskDescriptor TaskControl::describe() const {
    std::scoped_lock const lock{mutex_};
    return descriptor_;
}

Value::Object TaskControl::executionLogFields() const {
    std::scoped_lock const lock{mutex_};
    return taskLogFields(descriptor_.id, descriptor_.name, descriptor_.origin);
}

TaskState TaskControl::state() const {
    std::scoped_lock const lock{mutex_};
    return descriptor_.state;
}

Progress TaskControl::progress() const {
    std::scoped_lock const lock{mutex_};
    return descriptor_.progress;
}

CancellationToken TaskControl::cancellation() const noexcept {
    return CancellationToken{cancellation_};
}

bool TaskControl::requestCancel() {
    std::optional<TaskDescriptor> notification;
    {
        std::scoped_lock const lock{mutex_};
        if(descriptor_.state != TaskState::Pending) {
            if(isTerminal(descriptor_.state)) {
                return false;
            }
            cancellation_->store(true, std::memory_order_release);
            return false;
        }
        auto cancelled_result = cancelled_result_();
        cancellation_->store(true, std::memory_order_release);
        descriptor_.state = TaskState::Cancelled;
        descriptor_.error = Error{.code = ErrorCode::Cancelled,
                                  .message = "Task was cancelled before execution",
                                  .path = std::nullopt,
                                  .details = std::nullopt};
        result_ = std::move(cancelled_result);
        notification = descriptor_;
    }
    publish(std::move(*notification));
    AXIOM_LOG_INFO(logger_, "task cancelled");
    return true;
}

bool TaskControl::start() {
    std::optional<TaskDescriptor> notification;
    {
        std::scoped_lock const lock{mutex_};
        if(descriptor_.state != TaskState::Pending) {
            return false;
        }
        descriptor_.state = TaskState::Running;
        worker_thread_ = std::this_thread::get_id();
        notification = descriptor_;
    }
    publish(std::move(*notification));
    AXIOM_LOG_INFO(logger_, "task started");
    return true;
}

void TaskControl::reportProgress(const double value, std::string message) {
    if(!std::isfinite(value) || value < 0.0 || value > 1.0) {
        throw std::invalid_argument{"Task progress must be finite and within [0, 1]"};
    }
    std::optional<TaskDescriptor> notification;
    {
        std::scoped_lock const lock{mutex_};
        if(descriptor_.state != TaskState::Running) {
            return;
        }
        if(std::this_thread::get_id() != worker_thread_) {
            throw std::logic_error{"Task progress can only be reported on the execution thread"};
        }
        descriptor_.progress = {.value = value, .message = std::move(message)};
        notification = descriptor_;
    }
    publish(std::move(*notification));
    AXIOM_LOG_INFO(logger_, "task progress updated");
}

void TaskControl::complete(const TaskState terminal_state,
                           std::shared_ptr<const void> completed_result,
                           std::optional<Error> error) {
    std::optional<TaskDescriptor> notification;
    {
        std::scoped_lock const lock{mutex_};
        if(isTerminal(descriptor_.state)) {
            return;
        }
        descriptor_.state = terminal_state;
        if(terminal_state == TaskState::Completed) {
            descriptor_.progress = {.value = 1.0, .message = descriptor_.progress.message};
            descriptor_.error.reset();
        } else {
            descriptor_.error = std::move(error);
        }
        result_ = std::move(completed_result);
        notification = descriptor_;
    }
    publish(std::move(*notification));
    const auto level =
        terminal_state == TaskState::Failed ? logging::LogLevel::Error : logging::LogLevel::Info;
    const char* message = "task completed";
    if(terminal_state == TaskState::Failed) {
        message = "task failed";
    } else if(terminal_state == TaskState::Cancelled) {
        message = "task cancelled";
    }
    AXIOM_LOG(logger_, level, Value::Object{}, "{}", message);
}

std::shared_ptr<const void> TaskControl::result() const {
    std::scoped_lock const lock{mutex_};
    return result_;
}

void TaskControl::publish(TaskDescriptor descriptor) {
    bool drain = false;
    {
        std::scoped_lock const lock{notification_mutex_};
        notifications_queue_.push_back(std::move(descriptor));
        if(!draining_) {
            draining_ = true;
            drain = true;
        }
    }
    if(drain) {
        drainNotifications();
    }
}

void TaskControl::drainNotifications() noexcept {
    for(;;) {
        std::optional<TaskDescriptor> descriptor;
        {
            std::scoped_lock const lock{notification_mutex_};
            if(notifications_queue_.empty()) {
                draining_ = false;
                return;
            }
            descriptor = std::move(notifications_queue_.front());
            notifications_queue_.pop_front();
        }
        if(const auto notifications = notifications_.lock()) {
            notifications->emit(*descriptor);
        }
    }
}

} // namespace axiom::task::detail
