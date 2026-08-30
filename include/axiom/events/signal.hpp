#pragma once

/**
 * @file signal.hpp
 * @brief Ordered, thread-safe event subscriptions.
 */

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace axiom::events {

/** @brief Identifies a connection within a signal. */
using ConnectionId = std::uint64_t;

/**
 * @brief Publishes values to a set of ordered slots.
 *
 * Slots are invoked in connection order. Emission takes a snapshot while holding
 * the signal lock, then invokes that snapshot without the lock: connection changes
 * made by a slot affect only later emissions. A slot exception is propagated and
 * stops the current emission.
 *
 * @tparam Args Values delivered to each connected slot.
 */
template <typename... Args> class Signal final {
private:
    using Slot = std::function<void(Args...)>;

    struct State final {
        std::mutex mutex;
        std::map<ConnectionId, Slot> slots;
        ConnectionId next_id{1};
    };

public:
    /**
     * @brief RAII ownership of one signal connection.
     *
     * Destroying or resetting a subscription disconnects its slot. It is safe to
     * reset a subscription after its originating signal has been destroyed.
     */
    class Subscription final {
    public:
        /** @brief Creates an inactive subscription. */
        Subscription() = default;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;

        /** @brief Transfers disconnection ownership from @p other. */
        Subscription(Subscription&& other) noexcept
            : state_(std::move(other.state_)), id_(std::exchange(other.id_, 0)) {}

        /** @brief Disconnects the current slot and takes ownership from @p other. */
        Subscription& operator=(Subscription&& other) noexcept {
            if(this != &other) {
                reset();
                state_ = std::move(other.state_);
                id_ = std::exchange(other.id_, 0);
            }
            return *this;
        }

        /** @brief Disconnects the slot if it remains connected. */
        ~Subscription() { reset(); }

        /**
         * @brief Disconnects this slot and makes this subscription inactive.
         * @return true when a live slot was disconnected; otherwise false.
         */
        bool reset() noexcept {
            const auto state = state_.lock();
            const auto id = std::exchange(id_, 0);
            state_.reset();
            if(!state || id == 0) {
                return false;
            }

            std::lock_guard lock{state->mutex};
            return state->slots.erase(id) != 0;
        }

        /** @brief Reports whether this subscription still owns a connected slot. */
        [[nodiscard]] bool active() const noexcept {
            const auto state = state_.lock();
            if(!state || id_ == 0) {
                return false;
            }
            std::lock_guard lock{state->mutex};
            return state->slots.contains(id_);
        }

        /** @brief Returns this connection's identifier, or zero when inactive. */
        [[nodiscard]] ConnectionId id() const noexcept { return id_; }

    private:
        friend class Signal;

        Subscription(const std::shared_ptr<State>& state, const ConnectionId id)
            : state_(state), id_(id) {}

        std::weak_ptr<State> state_;
        ConnectionId id_{0};
    };

    /** @brief Creates an empty signal. */
    Signal() : state_(std::make_shared<State>()) {}
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    Signal(Signal&&) = delete;
    Signal& operator=(Signal&&) = delete;
    ~Signal() = default;

    /**
     * @brief Connects a slot to this signal.
     * @param slot Callable invoked by later emissions.
     * @return Move-only subscription that owns the new connection.
     * @throws std::invalid_argument if @p slot is empty.
     */
    [[nodiscard]] Subscription connect(Slot slot) {
        if(!slot) {
            throw std::invalid_argument{"Signal slot must not be empty"};
        }

        std::lock_guard lock{state_->mutex};
        const auto id = state_->next_id++;
        state_->slots.emplace(id, std::move(slot));
        return Subscription{state_, id};
    }

    /**
     * @brief Disconnects a slot by identifier.
     * @param id Identifier returned internally for the connection.
     * @return true if a connected slot was removed.
     */
    bool disconnect(const ConnectionId id) noexcept {
        std::lock_guard lock{state_->mutex};
        return state_->slots.erase(id) != 0;
    }

    /**
     * @brief Invokes a snapshot of currently connected slots in insertion order.
     * @param args Values delivered to each slot.
     * @throws Any exception thrown by the first failing slot.
     */
    void emit(Args... args) const {
        std::vector<Slot> snapshot;
        {
            std::lock_guard lock{state_->mutex};
            snapshot.reserve(state_->slots.size());
            for(const auto& [_, slot] : state_->slots) {
                snapshot.push_back(slot);
            }
        }

        for(const auto& slot : snapshot) {
            slot(args...);
        }
    }

private:
    std::shared_ptr<State> state_;
};

} // namespace axiom::events
