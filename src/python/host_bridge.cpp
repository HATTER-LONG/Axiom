#include <axiom/python/host_bridge.hpp>

#ifdef AXIOM_ENABLE_TEST_SEAMS
#include "dispatch_fault.hpp"
#endif

#include <axiom/action/invocation_context.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/command/command_dispatcher.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/task/task_registry.hpp>

#ifdef AXIOM_ENABLE_TEST_SEAMS
#include <atomic>
#endif
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#ifdef AXIOM_ENABLE_TEST_SEAMS
#include <stdexcept>
#endif
#include <string_view>
#include <utility>

namespace axiom::python {
namespace {

[[nodiscard]] Error notAttachedError() {
    return {.code = ErrorCode::HostClosed,
            .message = "Host session is not attached",
            .path = std::nullopt,
            .details = std::nullopt};
}

[[nodiscard]] Error closedError() {
    return {.code = ErrorCode::HostClosed,
            .message = "Host session is closed",
            .path = std::nullopt,
            .details = std::nullopt};
}

using axiom::command::CommandDispatcher;

thread_local const HostState* active_dispatch_state = nullptr;

#ifdef AXIOM_ENABLE_TEST_SEAMS
std::atomic<int>& dispatchFaultDepth() noexcept {
    static std::atomic<int> depth{0};
    return depth;
}

void throwIfDispatchFaultArmed() {
    if(dispatchFaultDepth().load(std::memory_order_acquire) != 0) {
        throw std::runtime_error{"Injected host dispatch fault"};
    }
}
#endif

} // namespace

class HostState {
public:
    HostState(const Runtime& runtime,
              const resource::ResourceRegistry& resources,
              task::TaskRegistry& tasks)
        : dispatcher_(std::make_unique<CommandDispatcher>(runtime, resources, tasks)) {}

    ~HostState() noexcept { static_cast<void>(close()); }

    HostState(const HostState&) = delete;
    HostState& operator=(const HostState&) = delete;
    HostState(HostState&&) = delete;
    HostState& operator=(HostState&&) = delete;

    [[nodiscard]] Result<Value> dispatch(const std::string_view method,
                                         const Value::Object& params,
                                         const InvocationContext& context) {
        const auto lease = acquireLease();
        if(!lease.has_value()) {
            return Result<Value>::failure(closedError());
        }
        // The lease releases the active count on every exit path, including
        // unexpected exceptions escaping after the lease is held.
#ifdef AXIOM_ENABLE_TEST_SEAMS
        throwIfDispatchFaultArmed();
#endif
        return lease->dispatcher().dispatch(method, params, context);
    }

    [[nodiscard]] bool closed() const noexcept {
        const std::scoped_lock lock(mutex_);
        return closed_ || !dispatcher_;
    }

    bool close() noexcept {
        if(active_dispatch_state == this) {
            return false;
        }
        try {
            std::unique_lock lock(mutex_);
            closed_ = true;
            drained_.wait(lock, [this] { return active_ == 0U; });
            dispatcher_.reset();
            return true;
        }
        // NOLINTNEXTLINE(bugprone-empty-catch): teardown must never propagate.
        catch(...) {
            return false;
        }
    }

private:
    // RAII guard over one in-flight dispatch: the active count is released no
    // matter how the dispatch exits, so close() can never wedge waiting for a
    // lease whose dispatch threw.
    class DispatchLease final {
    public:
        DispatchLease(HostState& state, CommandDispatcher& dispatcher) noexcept
            : state_(&state), dispatcher_(&dispatcher), previous_(active_dispatch_state) {
            active_dispatch_state = &state;
        }

        DispatchLease(DispatchLease&& other) noexcept
            : state_(std::exchange(other.state_, nullptr)), dispatcher_(other.dispatcher_),
              previous_(other.previous_) {}

        DispatchLease(const DispatchLease&) = delete;
        DispatchLease& operator=(const DispatchLease&) = delete;
        DispatchLease& operator=(DispatchLease&&) = delete;

        ~DispatchLease() {
            if(state_ == nullptr) {
                return;
            }
            const std::scoped_lock lock(state_->mutex_);
            --state_->active_;
            if(state_->active_ == 0U) {
                state_->drained_.notify_all();
            }
            active_dispatch_state = previous_;
        }

        [[nodiscard]] CommandDispatcher& dispatcher() const noexcept { return *dispatcher_; }

    private:
        HostState* state_;
        CommandDispatcher* dispatcher_;
        const HostState* previous_;
    };

    // Grants a lease unless the session is closed or already torn down. The
    // dispatcher pointer captured in the lease stays valid: close() only
    // resets the dispatcher after every lease has been released.
    [[nodiscard]] std::optional<DispatchLease> acquireLease() {
        const std::scoped_lock lock(mutex_);
        if(closed_ || !dispatcher_) {
            return std::nullopt;
        }
        ++active_;
        return DispatchLease{*this, *dispatcher_};
    }

    mutable std::mutex mutex_;
    std::condition_variable drained_;
    std::unique_ptr<CommandDispatcher> dispatcher_;
    std::size_t active_{0U};
    bool closed_{false};
};

HostBridge::HostBridge(const Runtime& runtime,
                       const resource::ResourceRegistry& resources,
                       task::TaskRegistry& tasks)
    : state_(std::make_shared<HostState>(runtime, resources, tasks)) {}

HostBridge::~HostBridge() noexcept {
    if(state_) {
        static_cast<void>(state_->close());
    }
}

HostBridge::HostBridge(HostBridge&&) noexcept = default;

HostBridge& HostBridge::operator=(HostBridge&& other) noexcept {
    if(this != &other) {
        if(state_) {
            static_cast<void>(state_->close());
        }
        state_ = std::move(other.state_);
    }
    return *this;
}

bool HostBridge::close() noexcept {
    if(state_) {
        return state_->close();
    }
    return true;
}

bool HostBridge::closed() const noexcept { return !state_ || state_->closed(); }

HostHandle HostBridge::attach() const noexcept { return HostHandle{state_}; }

Result<Value> HostBridge::dispatch(const std::string_view method,
                                   const Value::Object& params,
                                   const InvocationContext& context) const {
    if(!state_) {
        return Result<Value>::failure(notAttachedError());
    }
    return state_->dispatch(method, params, context);
}

HostHandle::HostHandle(std::shared_ptr<HostState> state) noexcept : state_(std::move(state)) {}

bool HostHandle::empty() const noexcept { return !state_; }

bool HostHandle::closed() const noexcept { return !state_ || state_->closed(); }

Result<Value> HostHandle::dispatch(const std::string_view method,
                                   const Value::Object& params,
                                   const InvocationContext& context) const {
    if(!state_) {
        return Result<Value>::failure(notAttachedError());
    }
    return state_->dispatch(method, params, context);
}

#ifdef AXIOM_ENABLE_TEST_SEAMS
namespace detail {

DispatchFaultGuard::DispatchFaultGuard() {
    dispatchFaultDepth().fetch_add(1, std::memory_order_release);
}

DispatchFaultGuard::~DispatchFaultGuard() {
    dispatchFaultDepth().fetch_sub(1, std::memory_order_release);
}

} // namespace detail
#endif

} // namespace axiom::python
