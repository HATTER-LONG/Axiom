#pragma once

/**
 * @file scheduler.hpp
 * @brief Timed callback dispatch through an Executor.
 */

#include <axiom/export.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace axiom::async {

class Executor;

/**
 * @brief Schedules callbacks for dispatch by an Executor.
 *
 * The scheduler owns a timing thread but never invokes user callbacks there.
 * Its destructor cancels pending work and joins the timing thread; the referenced
 * Executor must outlive the Scheduler.
 */
class AXIOM_API Scheduler final {
private:
    struct State;

public:
    /** @brief Move-only ownership of one scheduled callback. */
    class ScheduleHandle final {
    public:
        /** @brief Creates an inactive handle. */
        ScheduleHandle() = default;
        ScheduleHandle(const ScheduleHandle&) = delete;
        ScheduleHandle& operator=(const ScheduleHandle&) = delete;
        /** @brief Transfers cancellation ownership from @p other. */
        ScheduleHandle(ScheduleHandle&& other) noexcept;
        /** @brief Cancels this handle before taking ownership from @p other. */
        ScheduleHandle& operator=(ScheduleHandle&& other) noexcept;
        /** @brief Cancels a pending callback, if any. */
        ~ScheduleHandle();

        /**
         * @brief Cancels this callback and makes the handle inactive.
         * @return true if the callback was pending; otherwise false.
         */
        bool cancel() noexcept;

        /** @brief Alias for cancel(). */
        bool reset() noexcept { return cancel(); }

        /** @brief Reports whether this handle still refers to a pending callback. */
        [[nodiscard]] bool active() const noexcept;

    private:
        friend class Scheduler;
        ScheduleHandle(std::weak_ptr<State> state, std::uint64_t id) noexcept;

        std::weak_ptr<State> state_;
        std::uint64_t id_{0};
    };

    /**
     * @brief Creates a scheduler that dispatches through @p executor.
     * @param executor Executor that must outlive this scheduler.
     */
    explicit Scheduler(Executor& executor);
    /** @brief Cancels pending callbacks and joins the timing thread. */
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    /**
     * @brief Schedules one callback after a delay.
     * @param delay Non-negative delay from now.
     * @param callback Callback dispatched through the executor.
     * @return Handle that can cancel the callback.
     * @throws std::invalid_argument if delay is negative or callback is empty.
     * @throws std::runtime_error if the executor no longer accepts work.
     */
    [[nodiscard]] ScheduleHandle scheduleAfter(std::chrono::steady_clock::duration delay,
                                               std::function<void()> callback);

    /**
     * @brief Schedules a callback repeatedly at a positive period.
     * @param period Positive period between due times.
     * @param callback Callback dispatched through the executor.
     * @return Handle that can cancel future dispatches.
     * @throws std::invalid_argument if period is not positive or callback is empty.
     * @throws std::runtime_error if the executor no longer accepts work.
     */
    [[nodiscard]] ScheduleHandle scheduleEvery(std::chrono::steady_clock::duration period,
                                               std::function<void()> callback);

private:
    [[nodiscard]] ScheduleHandle schedule(std::chrono::steady_clock::duration delay,
                                          std::chrono::steady_clock::duration period,
                                          std::function<void()> callback);
    static bool cancel(const std::shared_ptr<State>& state, std::uint64_t id) noexcept;
    [[nodiscard]] static bool active(const std::shared_ptr<State>& state,
                                     std::uint64_t id) noexcept;
    static void run(const std::shared_ptr<State>& state);

    std::shared_ptr<State> state_;
};

} // namespace axiom::async
#include <axiom/export.hpp>
