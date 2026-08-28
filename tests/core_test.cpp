#include <axiom/core/core.hpp>

#include <gtest/gtest.h>

TEST(CoreFrameworkName, ReturnsAxiom) { EXPECT_STREQ(axiom::core::frameworkName(), "Axiom"); }

TEST(CoreFrameworkName, AcceptsExactName) { EXPECT_TRUE(axiom::core::isFrameworkName("Axiom")); }

TEST(CoreFrameworkName, RejectsDifferentName) {
    EXPECT_FALSE(axiom::core::isFrameworkName("Other"));
}
