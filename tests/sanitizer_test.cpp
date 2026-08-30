#include <gtest/gtest.h>

#include <limits>

namespace {

TEST(SanitizerDeathTest, UndefinedBehaviorStopsTheProcess) {
    // Recovery would let the statement complete and make this regression test fail.
    EXPECT_DEATH(
        {
            volatile int largest = std::numeric_limits<int>::max();
            volatile int overflow = largest + 1;
            static_cast<void>(overflow);
        },
        "runtime error: signed integer overflow");
}

} // namespace
