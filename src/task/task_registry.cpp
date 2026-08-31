#include <axiom/task/task_registry.hpp>

#include <axiom/async/executor.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/logging/logger.hpp>
#include <axiom/task/detail/task_control.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_types.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace axiom::task {
namespace {
[[nodiscard]] Error missingTask() {
    return {.code = ErrorCode::NotFound,
            .message = "Task was not found",
            .path = std::nullopt,
            .details = std::nullopt};
}
[[nodiscard]] Error activeTask() {
    return {.code = ErrorCode::InvalidArgument,
            .message = "Only terminal tasks can be removed",
            .path = std::nullopt,
            .details = std::nullopt};
}
} // namespace

struct TaskRegistry::Impl final {
    explicit Impl(logging::Logger initial_logger)
        : notifications(std::make_shared<detail::NotificationHub>(initial_logger)),
          logger(std::move(initial_logger)) {}
    mutable std::mutex mutex;
    std::map<TaskId, std::shared_ptr<detail::TaskControl>> tasks;
    std::shared_ptr<detail::NotificationHub> notifications;
    logging::Logger logger;
};

TaskRegistry::TaskRegistry() : TaskRegistry(logging::Logger{}) {}
TaskRegistry::TaskRegistry(logging::Logger logger)
    : impl_(std::make_shared<Impl>(std::move(logger))) {}
TaskRegistry::~TaskRegistry() noexcept { impl_->notifications->close(); }

Result<std::shared_ptr<detail::TaskControl>> TaskRegistry::submitControl(
    async::Executor& executor,
    std::string name,
    std::function<void(const std::shared_ptr<detail::TaskControl>&)> function,
    std::function<std::shared_ptr<const void>()> cancelled_result) {
    static std::atomic_uint64_t next_id{1};
    auto serial = next_id.load(std::memory_order_relaxed);
    for(;;) {
        if(serial == std::numeric_limits<std::uint64_t>::max()) {
            return Result<std::shared_ptr<detail::TaskControl>>::failure(
                {.code = ErrorCode::InternalError,
                 .message = "Task identifier serials are exhausted",
                 .path = std::nullopt,
                 .details = std::nullopt});
        }
        if(next_id.compare_exchange_weak(serial, serial + 1U, std::memory_order_relaxed)) {
            break;
        }
    }
    auto id = TaskId::parse("task:" + std::to_string(serial));
    const auto descriptor_id = id.value();
    auto task_logger = impl_->logger.child("task").withFields(
        {{"task_id", Value{std::string{descriptor_id.str()}}}, {"task_name", Value{name}}});
    auto control = std::make_shared<detail::TaskControl>(
        std::move(id.value()), std::move(name), impl_->notifications, std::move(task_logger),
        std::move(cancelled_result));
    const auto control_id = control->describe().id;
    {
        std::scoped_lock const lock{impl_->mutex};
        impl_->tasks.emplace(control_id, control);
    }
    try {
        [[maybe_unused]] auto accepted = executor.submit(
            [control, function = std::move(function)]() mutable { function(control); });
    } catch(const std::runtime_error&) {
        std::scoped_lock const lock{impl_->mutex};
        impl_->tasks.erase(control_id);
        return Result<std::shared_ptr<detail::TaskControl>>::failure(
            {.code = ErrorCode::InvalidArgument,
             .message = "Executor is no longer accepting tasks",
             .path = std::nullopt,
             .details = std::nullopt});
    } catch(...) {
        std::scoped_lock const lock{impl_->mutex};
        impl_->tasks.erase(control_id);
        throw;
    }
    return Result<std::shared_ptr<detail::TaskControl>>::success(std::move(control));
}

Result<TaskDescriptor> TaskRegistry::describe(const TaskId& id) const {
    std::shared_ptr<detail::TaskControl> control;
    {
        std::scoped_lock const lock{impl_->mutex};
        const auto found = impl_->tasks.find(id);
        if(found == impl_->tasks.end()) {
            return Result<TaskDescriptor>::failure(missingTask());
        }
        control = found->second;
    }
    return Result<TaskDescriptor>::success(control->describe());
}

std::vector<TaskDescriptor> TaskRegistry::list() const {
    std::vector<std::shared_ptr<detail::TaskControl>> controls;
    {
        std::scoped_lock const lock{impl_->mutex};
        controls.reserve(impl_->tasks.size());
        for(const auto& [_, control] : impl_->tasks) {
            controls.push_back(control);
        }
    }
    std::vector<TaskDescriptor> result;
    result.reserve(controls.size());
    std::ranges::transform(controls, std::back_inserter(result),
                           [](const auto& control) { return control->describe(); });
    return result;
}

Result<void> TaskRegistry::cancel(const TaskId& id) {
    std::shared_ptr<detail::TaskControl> control;
    {
        std::scoped_lock const lock{impl_->mutex};
        const auto found = impl_->tasks.find(id);
        if(found == impl_->tasks.end()) {
            return Result<void>::failure(missingTask());
        }
        control = found->second;
    }
    static_cast<void>(control->requestCancel());
    return Result<void>::success();
}

Result<void> TaskRegistry::remove(const TaskId& id) {
    std::shared_ptr<detail::TaskControl> removed;
    {
        std::scoped_lock const lock{impl_->mutex};
        const auto found = impl_->tasks.find(id);
        if(found == impl_->tasks.end()) {
            return Result<void>::failure(missingTask());
        }
        if(!isTerminal(found->second->state())) {
            return Result<void>::failure(activeTask());
        }
        removed = std::move(found->second);
        impl_->tasks.erase(found);
    }
    return Result<void>::success();
}

TaskRegistry::Subscription
TaskRegistry::onChanged(std::function<void(const TaskDescriptor&)> callback) {
    return impl_->notifications->connect(std::move(callback));
}

bool CancellationToken::requested() const noexcept {
    return requested_ && requested_->load(std::memory_order_acquire);
}

const TaskId& TaskContext::id() const noexcept { return control_->id(); }
CancellationToken TaskContext::cancellation() const noexcept { return control_->cancellation(); }
void TaskContext::reportProgress(const double value, std::string message) {
    control_->reportProgress(value, std::move(message));
}

} // namespace axiom::task
