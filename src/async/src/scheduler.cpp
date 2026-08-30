#include <axiom/async/scheduler.hpp>

#include <axiom/async/executor.hpp>

#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace axiom::async {

struct Scheduler::State final {
    struct Entry final {
        std::chrono::steady_clock::time_point due;
        std::chrono::steady_clock::duration period;
        std::function<void()> callback;
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
    std::lock_guard lock{state->mutex};
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
    state->entries.erase(entry);
    state->changed.notify_all();
    return true;
}

bool Scheduler::active(const std::shared_ptr<State>& state, const std::uint64_t id) noexcept {
    if(!state || id == 0) {
        return false;
    }
    std::lock_guard lock{state->mutex};
    return state->entries.contains(id);
}

void Scheduler::run(const std::shared_ptr<State>& state) {
    std::unique_lock lock{state->mutex};
    while(!state->stopping) {
        if(state->due_entries.empty()) {
            state->changed.wait(
                lock, [&state] { return state->stopping || !state->due_entries.empty(); });
            continue;
        }

        const auto due_iterator = state->due_entries.begin();
        const auto due = due_iterator->first;
        if(state->changed.wait_until(lock, due, [&state, due] {
               return state->stopping || state->due_entries.empty() ||
                      state->due_entries.begin()->first < due;
           })) {
            continue;
        }
        if(state->stopping || state->due_entries.empty() ||
           state->due_entries.begin()->first != due) {
            continue;
        }

        const auto id = state->due_entries.begin()->second;
        state->due_entries.erase(state->due_entries.begin());
        const auto entry = state->entries.find(id);
        if(entry == state->entries.end()) {
            continue;
        }

        auto callback = entry->second.callback;
        if(entry->second.period > std::chrono::steady_clock::duration::zero()) {
            entry->second.due = due + entry->second.period;
            state->due_entries.emplace(entry->second.due, id);
        } else {
            state->entries.erase(entry);
        }

        lock.unlock();
        try {
            [[maybe_unused]] auto dispatched =
                state->executor.submit([callback = std::move(callback)]() mutable { callback(); });
        } catch(const std::runtime_error&) {
            cancel(state, id);
        }
        lock.lock();
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
    {
        std::lock_guard lock{state_->mutex};
        state_->stopping = true;
        state_->entries.clear();
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
    return schedule(delay, std::chrono::steady_clock::duration::zero(), std::move(callback));
}

Scheduler::ScheduleHandle Scheduler::scheduleEvery(const std::chrono::steady_clock::duration period,
                                                   std::function<void()> callback) {
    if(period <= std::chrono::steady_clock::duration::zero()) {
        throw std::invalid_argument{"Scheduler period must be positive"};
    }
    return schedule(period, period, std::move(callback));
}

Scheduler::ScheduleHandle Scheduler::schedule(const std::chrono::steady_clock::duration delay,
                                              const std::chrono::steady_clock::duration period,
                                              std::function<void()> callback) {
    if(!callback) {
        throw std::invalid_argument{"Scheduler callback must not be empty"};
    }
    if(!state_->executor.accepting()) {
        throw std::runtime_error{"Scheduler executor is closed"};
    }

    std::lock_guard lock{state_->mutex};
    if(state_->stopping) {
        throw std::runtime_error{"Scheduler is stopped"};
    }
    const auto id = state_->next_id++;
    const auto due = std::chrono::steady_clock::now() + delay;
    state_->entries.emplace(id, State::Entry{due, period, std::move(callback)});
    state_->due_entries.emplace(due, id);
    state_->changed.notify_all();
    return ScheduleHandle{state_, id};
}

} // namespace axiom::async
