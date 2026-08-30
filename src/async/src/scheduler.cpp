#include <axiom/async/scheduler.hpp>

#include <axiom/async/executor.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace axiom::async {

namespace {
[[nodiscard]] std::chrono::steady_clock::time_point
deadlineAfter(const std::chrono::steady_clock::duration delay) {
    const auto now = std::chrono::steady_clock::now();
    if(now > std::chrono::steady_clock::time_point::max() - delay) {
        throw std::overflow_error{"Scheduler deadline is outside the clock range"};
    }
    return now + delay;
}
} // namespace

struct Scheduler::State final {
    struct Entry final {
        std::chrono::steady_clock::time_point due;
        std::chrono::steady_clock::duration period;
        std::shared_ptr<std::function<void()>> callback;
    };

    explicit State(Executor& executor_ref) : executor(executor_ref) {}

    Executor& executor;
    std::mutex mutex;
    std::condition_variable changed;
    std::unordered_map<std::uint64_t, Entry> entries;
    std::multimap<std::chrono::steady_clock::time_point, std::uint64_t> due_entries;
    std::thread worker;
    std::uint64_t next_id{1};
    bool stopping{false};
};

bool Scheduler::cancel(const std::shared_ptr<State>& state, const std::uint64_t id) noexcept {
    if(!state || id == 0) {
        return false;
    }
    std::unique_lock lock{state->mutex};
    const auto entry = state->entries.find(id);
    if(entry == state->entries.end()) {
        return false;
    }
    const auto due = state->due_entries.equal_range(entry->second.due);
    for(auto iterator = due.first; iterator != due.second; ++iterator) {
        if(iterator->second == id) {
            state->due_entries.erase(iterator);
            break;
        }
    }
    // Captures may own other handles. Release them after unlocking so their
    // destructors can safely cancel another registration.
    auto removed = state->entries.extract(entry);
    lock.unlock();
    state->changed.notify_all();
    return true;
}

bool Scheduler::active(const std::shared_ptr<State>& state, const std::uint64_t id) noexcept {
    if(!state || id == 0) {
        return false;
    }
    std::scoped_lock const lock{state->mutex};
    return state->entries.contains(id);
}

bool Scheduler::waitForDue(State& state, std::unique_lock<std::mutex>& lock) {
    if(state.due_entries.empty()) {
        state.changed.wait(lock, [&state] { return state.stopping || !state.due_entries.empty(); });
        return false;
    }

    const auto due = state.due_entries.begin()->first;
    return !state.changed.wait_until(lock, due, [&state, due] {
        return state.stopping || state.due_entries.empty() ||
               state.due_entries.begin()->first != due;
    });
}

void Scheduler::dispatchDue(const std::shared_ptr<State>& state,
                            std::unique_lock<std::mutex>& lock) {
    if(state->stopping || state->due_entries.empty()) {
        return;
    }

    const auto due = state->due_entries.begin()->first;
    const auto id = state->due_entries.begin()->second;
    state->due_entries.erase(state->due_entries.begin());
    const auto entry = state->entries.find(id);
    if(entry == state->entries.end()) {
        return;
    }

    auto callback = entry->second.callback;
    if(entry->second.period > std::chrono::steady_clock::duration::zero() &&
       due <= std::chrono::steady_clock::time_point::max() - entry->second.period) {
        entry->second.due = due + entry->second.period;
        state->due_entries.emplace(entry->second.due, id);
    } else {
        state->entries.erase(entry);
    }

    lock.unlock();
    try {
        [[maybe_unused]] auto dispatched = state->executor.submit([callback] { (*callback)(); });
    } catch(const std::runtime_error&) {
        cancel(state, id);
    }
    callback.reset();
    lock.lock();
}

void Scheduler::run(const std::shared_ptr<State>& state) {
    std::unique_lock lock{state->mutex};
    while(!state->stopping) {
        if(waitForDue(*state, lock)) {
            dispatchDue(state, lock);
        }
    }
}

Scheduler::ScheduleHandle::ScheduleHandle(std::weak_ptr<State> state,
                                          const std::uint64_t id) noexcept
    : state_(std::move(state)), id_(id) {}

Scheduler::ScheduleHandle::ScheduleHandle(ScheduleHandle&& other) noexcept
    : state_(std::move(other.state_)), id_(std::exchange(other.id_, 0)) {}

Scheduler::ScheduleHandle& Scheduler::ScheduleHandle::operator=(ScheduleHandle&& other) noexcept {
    if(this != &other) {
        cancel();
        state_ = std::move(other.state_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

Scheduler::ScheduleHandle::~ScheduleHandle() { cancel(); }

bool Scheduler::ScheduleHandle::cancel() noexcept {
    const auto state = state_.lock();
    const auto id = std::exchange(id_, 0);
    state_.reset();
    return Scheduler::cancel(state, id);
}

bool Scheduler::ScheduleHandle::active() const noexcept {
    return Scheduler::active(state_.lock(), id_);
}

Scheduler::Scheduler(Executor& executor) : state_(std::make_shared<State>(executor)) {
    state_->worker = std::thread([state = state_] { Scheduler::run(state); });
}

Scheduler::~Scheduler() {
    decltype(state_->entries) removed;
    {
        std::scoped_lock const lock{state_->mutex};
        state_->stopping = true;
        removed.swap(state_->entries);
        state_->due_entries.clear();
    }
    state_->changed.notify_all();
    if(state_->worker.joinable()) {
        state_->worker.join();
    }
}

Scheduler::ScheduleHandle Scheduler::scheduleAfter(const std::chrono::steady_clock::duration delay,
                                                   std::function<void()> callback) {
    if(delay < std::chrono::steady_clock::duration::zero()) {
        throw std::invalid_argument{"Scheduler delay must not be negative"};
    }
    return schedule(deadlineAfter(delay), std::chrono::steady_clock::duration::zero(),
                    std::move(callback));
}

Scheduler::ScheduleHandle Scheduler::scheduleEvery(const std::chrono::steady_clock::duration period,
                                                   std::function<void()> callback) {
    if(period <= std::chrono::steady_clock::duration::zero()) {
        throw std::invalid_argument{"Scheduler period must be positive"};
    }
    return schedule(deadlineAfter(period), period, std::move(callback));
}

Scheduler::ScheduleHandle Scheduler::schedule(const std::chrono::steady_clock::time_point due,
                                              const std::chrono::steady_clock::duration period,
                                              std::function<void()> callback) {
    if(!callback) {
        throw std::invalid_argument{"Scheduler callback must not be empty"};
    }
    if(!state_->executor.accepting()) {
        throw std::runtime_error{"Scheduler executor is closed"};
    }

    auto owned_callback = std::make_shared<std::function<void()>>(std::move(callback));
    std::scoped_lock const lock{state_->mutex};
    if(state_->stopping) {
        throw std::runtime_error{"Scheduler is stopped"};
    }
    const auto id = state_->next_id++;
    state_->entries.emplace(id,
                            State::Entry{.due = due, .period = period, .callback = owned_callback});
    try {
        state_->due_entries.emplace(due, id);
    } catch(...) {
        state_->entries.erase(id);
        throw;
    }
    state_->changed.notify_all();
    return ScheduleHandle{state_, id};
}

} // namespace axiom::async
