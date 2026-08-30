#include <axiom/events/signal.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

template <typename Exception, typename Callable> [[nodiscard]] bool throws(Callable&& callable) {
    try {
        std::forward<Callable>(callable)();
    } catch(const Exception&) {
        return true;
    } catch(...) {
        return false;
    }
    return false;
}

struct SubscriptionState final {
    bool connected{false};
    bool disconnected{false};
    bool inactive_before_destruction{false};
    bool reset_after_destruction{false};
    bool inactive_after_destruction{false};
};

[[nodiscard]] SubscriptionState exerciseSubscriptionLifetime() {
    axiom::events::Signal<>::Subscription subscription;
    SubscriptionState state;
    {
        axiom::events::Signal<> signal;
        subscription = signal.connect([] {});
        state.connected = subscription.active();
        state.disconnected = signal.disconnect(subscription.id());
        state.inactive_before_destruction = !subscription.active();
    }
    state.reset_after_destruction = subscription.reset();
    state.inactive_after_destruction = !subscription.active();
    return state;
}

struct ThrowingSlot final {
    void operator()() const { throw std::runtime_error{"failure"}; }
};

struct LaterSlot final {
    void operator()() { called = true; }

    bool called{false};
};

TEST(Signal, EmitsSlotsInConnectionOrder) {
    axiom::events::Signal<int> signal;
    std::vector<int> calls;
    const auto first = signal.connect([&calls](const int value) { calls.push_back(value); });
    const auto second = signal.connect([&calls](const int value) { calls.push_back(value * 2); });

    signal.emit(7);

    EXPECT_EQ(calls, (std::vector<int>{7, 14}));
    EXPECT_TRUE(first.active());
    EXPECT_TRUE(second.active());
}

TEST(Signal, UsesStableSnapshotWhenSlotDisconnectsAnotherSlot) {
    axiom::events::Signal<> signal;
    std::vector<std::string> calls;
    axiom::events::Signal<>::Subscription second;
    const auto first = signal.connect([&] {
        calls.emplace_back("first");
        second.reset();
    });
    second = signal.connect([&] { calls.emplace_back("second"); });

    signal.emit();
    signal.emit();

    EXPECT_EQ(calls, (std::vector<std::string>{"first", "second", "first"}));
    EXPECT_TRUE(first.active());
    EXPECT_FALSE(second.active());
}

TEST(Signal, SubscriptionDisconnectsAndIsSafeAfterSignalDestruction) {
    const auto state = exerciseSubscriptionLifetime();
    EXPECT_TRUE(state.connected);
    EXPECT_TRUE(state.disconnected);
    EXPECT_TRUE(state.inactive_before_destruction);
    EXPECT_FALSE(state.reset_after_destruction);
    EXPECT_TRUE(state.inactive_after_destruction);
}

TEST(Signal, PropagatesFirstSlotFailureAndStopsEmission) {
    axiom::events::Signal<> signal;
    const auto first = signal.connect(ThrowingSlot{});
    LaterSlot later;
    const auto second = signal.connect(std::ref(later));

    EXPECT_TRUE(throws<std::runtime_error>([&signal] { signal.emit(); }));
    EXPECT_FALSE(later.called);
    EXPECT_TRUE(first.active());
    EXPECT_TRUE(second.active());
}

TEST(Signal, RejectsEmptySlots) {
    axiom::events::Signal<> signal;
    EXPECT_TRUE(
        throws<std::invalid_argument>([&signal] { static_cast<void>(signal.connect({})); }));
}

TEST(Signal, PreservesMutableSlotStateAcrossEmissions) {
    axiom::events::Signal<> signal;
    std::vector<int> calls;
    const auto subscription =
        signal.connect([count = 0, &calls]() mutable { calls.push_back(++count); });
    signal.emit();
    signal.emit();
    EXPECT_EQ(calls, (std::vector<int>{1, 2}));
}

TEST(Signal, ReleasesCapturedSubscriptionsOutsideTheSignalLock) {
    axiom::events::Signal<> signal;
    auto inner = std::make_shared<axiom::events::Signal<>::Subscription>(signal.connect([] {}));
    const auto inner_id = inner->id();
    const std::weak_ptr<axiom::events::Signal<>::Subscription> weak = inner;
    auto outer = signal.connect([inner] {});
    inner = nullptr;

    EXPECT_TRUE(outer.reset());
    EXPECT_TRUE(weak.expired());
    EXPECT_FALSE(signal.disconnect(inner_id));
}

} // namespace
