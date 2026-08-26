#include <axiom/core/core.hpp>

#include <gtest/gtest.h>

TEST(Core, FrameworkNameIsAxiom) { EXPECT_STREQ(axiom::core::frameworkName(), "Axiom"); }
