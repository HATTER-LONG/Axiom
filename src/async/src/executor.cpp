#include <axiom/async/executor.hpp>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace axiom::async {

namespace {
thread_local const Executor* current_executor = nullptr;
}

struct Executor::State final {
    std::mutex mutex;
    std::mutex close_mutex;
    std::condition_variable available;
    std::deque<std::function<void()>> tasks;
    std::vector<std::thread> workers;
    bool closing{false};
};

Executor::Executor(std::size_t worker_count) : state_(std::make_unique<State>()) {
    if(worker_count == 0) {
        worker_count = 1;
    }

    state_->workers.reserve(worker_count);
    try {
        for(std::size_t index = 0; index < worker_count; ++index) {
            state_->workers.emplace_back([this] { runWorker(*state_, this); });
        }
    } catch(...) {
        {
            std::scoped_lock const lock{state_->mutex};
            state_->closing = true;
        }
        state_->available.notify_all();
        for(auto& worker : state_->workers) {
            if(worker.joinable()) {
                worker.join();
            }
        }
        throw;
    }
}

void Executor::runWorker(State& state, const Executor* const executor) {
    current_executor = executor;
    for(;;) {
        std::function<void()> task;
        {
            std::unique_lock lock{state.mutex};
            state.available.wait(lock, [&state] { return state.closing || !state.tasks.empty(); });
            if(state.tasks.empty()) {
                current_executor = nullptr;
                return;
            }
            task = std::move(state.tasks.front());
            state.tasks.pop_front();
        }
        task();
    }
}

Executor::~Executor() noexcept {
    if(current_executor == this) {
        std::terminate();
    }
    try {
        close();
    } catch(...) {
        std::terminate();
    }
}

void Executor::enqueue(std::function<void()> task) {
    {
        std::scoped_lock const lock{state_->mutex};
        if(state_->closing) {
            throw std::runtime_error{"Executor is closed"};
        }
        state_->tasks.push_back(std::move(task));
    }
    state_->available.notify_one();
}

bool Executor::accepting() const noexcept {
    std::scoped_lock const lock{state_->mutex};
    return !state_->closing;
}

void Executor::close() {
    if(current_executor == this) {
        throw std::logic_error{"Executor::close cannot be called by its worker"};
    }

    std::scoped_lock const close_lock{state_->close_mutex};

    {
        std::scoped_lock const lock{state_->mutex};
        state_->closing = true;
    }
    state_->available.notify_all();

    for(auto& worker : state_->workers) {
        if(worker.joinable()) {
            worker.join();
        }
    }
}

} // namespace axiom::async
