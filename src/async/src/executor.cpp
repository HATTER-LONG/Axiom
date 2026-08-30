#include <axiom/async/executor.hpp>

#include <condition_variable>
#include <deque>
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
            state_->workers.emplace_back([this] {
                current_executor = this;
                for(;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock lock{state_->mutex};
                        state_->available.wait(
                            lock, [this] { return state_->closing || !state_->tasks.empty(); });
                        if(state_->tasks.empty()) {
                            current_executor = nullptr;
                            return;
                        }
                        task = std::move(state_->tasks.front());
                        state_->tasks.pop_front();
                    }
                    task();
                }
            });
        }
    } catch(...) {
        {
            std::lock_guard lock{state_->mutex};
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
        std::lock_guard lock{state_->mutex};
        if(state_->closing) {
            throw std::runtime_error{"Executor is closed"};
        }
        state_->tasks.push_back(std::move(task));
    }
    state_->available.notify_one();
}

bool Executor::accepting() const noexcept {
    std::lock_guard lock{state_->mutex};
    return !state_->closing;
}

void Executor::close() {
    if(current_executor == this) {
        throw std::logic_error{"Executor::close cannot be called by its worker"};
    }

    std::lock_guard close_lock{state_->close_mutex};

    {
        std::lock_guard lock{state_->mutex};
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
