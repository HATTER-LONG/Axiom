#include "task_demo.hpp"

#include <axiom/axiom.hpp>

#include <future>
#include <iostream>

namespace {
[[nodiscard]] bool runCancellableTask(axiom::async::Executor& executor,
                                      axiom::task::TaskRegistry& tasks) {
    std::promise<void> checkpoint;
    std::promise<void> resume;
    auto reached = checkpoint.get_future();
    auto continuation = resume.get_future();
    auto submitted = tasks.submit(executor, "long operation", [&](axiom::task::TaskContext& ctx) {
        ctx.reportProgress(0.5, "First batch complete");
        checkpoint.set_value();
        // Stand in for a long operation at a deterministic work boundary.
        continuation.get();
        if(ctx.cancellation().requested()) {
            return axiom::Result<void>::failure({.code = axiom::ErrorCode::Cancelled,
                                                 .message = "Stopped between batches",
                                                 .path = {},
                                                 .details = {}});
        }
        return axiom::Result<void>::success();
    });
    if(!submitted) {
        return false;
    }
    const auto& handle = submitted.value();
    reached.get();
    const auto progress = handle.progress();
    std::cout << handle.id().str() << ": " << progress.message << " (" << progress.value << ")\n";
    handle.cancel();
    resume.set_value();
    executor.close();
    const auto result = handle.result();
    const auto removed = tasks.remove(handle.id());
    return result && result->hasError() && result->error().code == axiom::ErrorCode::Cancelled &&
           removed.hasValue() && handle.state() == axiom::task::TaskState::Cancelled;
}
} // namespace

bool runTaskDemo() {
    axiom::async::Executor executor{1};
    axiom::task::TaskRegistry tasks;
    auto answer = tasks.submit(executor, "answer", [](axiom::task::TaskContext& ctx) {
        ctx.reportProgress(0.25, "Computing answer");
        return axiom::Result<int>::success(42);
    });
    if(!answer || !runCancellableTask(executor, tasks)) {
        return false;
    }
    const auto result = answer.value().result();
    if(!result || !*result) {
        return false;
    }
    std::cout << answer.value().id().str() << ": answer = " << result->value() << '\n';
    return result->value() == 42 && tasks.remove(answer.value().id()).hasValue() &&
           tasks.list().empty();
}
