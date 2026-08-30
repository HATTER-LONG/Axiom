#include <testlib/core/core.hpp>

#include <gtest/gtest.h>

TEST(Version, IsPositive) { EXPECT_GT(testlib::core::version(), 0); }
