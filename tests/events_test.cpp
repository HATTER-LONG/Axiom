#include <axiom/events/signal.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

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
    axiom::events::Signal<>::Subscription subscription;
    {
        axiom::events::Signal<> signal;
        subscription = signal.connect([] {});
        EXPECT_TRUE(subscription.active());
        EXPECT_TRUE(signal.disconnect(subscription.id()));
        EXPECT_FALSE(subscription.active());
    }

    EXPECT_FALSE(subscription.reset());
    EXPECT_FALSE(subscription.active());
}

TEST(Signal, PropagatesFirstSlotFailureAndStopsEmission) {
    axiom::events::Signal<> signal;
    const auto first = signal.connect([] { throw std::runtime_error{"failure"}; });
    bool later_called = false;
    const auto second = signal.connect([&] { later_called = true; });

    EXPECT_THROW(signal.emit(), std::runtime_error);
    EXPECT_FALSE(later_called);
    EXPECT_TRUE(first.active());
    EXPECT_TRUE(second.active());
}

TEST(Signal, RejectsEmptySlots) {
    axiom::events::Signal<> signal;
    const auto connect_empty = [&signal] {
        auto subscription = signal.connect({});
        static_cast<void>(subscription);
    };
    EXPECT_THROW(connect_empty(), std::invalid_argument);
}

} // namespace
