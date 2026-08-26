#include <axiom/core/core.hpp>

#include <gtest/gtest.h>

#include <array>

TEST(Core, FrameworkNameIsAxiom) { EXPECT_STREQ(axiom::core::frameworkName(), "Axiom"); }

TEST(Core, SummarizeScoresEmptyInputIsInvalid) {
    const auto summary = axiom::core::summarizeScores({});
    EXPECT_EQ(summary.m_samples, 0);
    EXPECT_DOUBLE_EQ(summary.m_average, 0.0);
    EXPECT_EQ(summary.m_grade, axiom::core::QualityGrade::Invalid);
}

TEST(Core, SummarizeScoresFailingRange) {
    const std::array scores = {10, 20, 60};
    const auto summary = axiom::core::summarizeScores(scores);
    EXPECT_EQ(summary.m_samples, 3);
    EXPECT_EQ(summary.m_minimum, 10);
    EXPECT_EQ(summary.m_maximum, 60);
    EXPECT_DOUBLE_EQ(summary.m_average, 30.0);
    EXPECT_EQ(summary.m_grade, axiom::core::QualityGrade::Failing);
}

TEST(Core, SummarizeScoresPassingRange) {
    const std::array scores = {70, 80, 90};
    const auto summary = axiom::core::summarizeScores(scores);
    EXPECT_EQ(summary.m_samples, 3);
    EXPECT_EQ(summary.m_minimum, 70);
    EXPECT_EQ(summary.m_maximum, 90);
    EXPECT_DOUBLE_EQ(summary.m_average, 80.0);
    EXPECT_EQ(summary.m_grade, axiom::core::QualityGrade::Passing);
}

TEST(Core, SummarizeScoresExcellentRange) {
    const std::array scores = {90, 95, 100};
    const auto summary = axiom::core::summarizeScores(scores);
    EXPECT_EQ(summary.m_samples, 3);
    EXPECT_EQ(summary.m_minimum, 90);
    EXPECT_EQ(summary.m_maximum, 100);
    EXPECT_DOUBLE_EQ(summary.m_average, 95.0);
    EXPECT_EQ(summary.m_grade, axiom::core::QualityGrade::Excellent);
}

TEST(Core, SummarizeScoresAboveRangeIsInvalid) {
    const std::array scores = {60, 101};
    const auto summary = axiom::core::summarizeScores(scores);
    EXPECT_EQ(summary.m_samples, 0);
    EXPECT_DOUBLE_EQ(summary.m_average, 0.0);
    EXPECT_EQ(summary.m_grade, axiom::core::QualityGrade::Invalid);
}

TEST(Core, SummarizeScoresNegativeIsInvalid) {
    const std::array scores = {90, -1};
    const auto summary = axiom::core::summarizeScores(scores);
    EXPECT_EQ(summary.m_samples, 0);
    EXPECT_EQ(summary.m_grade, axiom::core::QualityGrade::Invalid);
}

TEST(Core, SummarizeScoresDescendingSequenceTracksMinimum) {
    const std::array scores = {80, 40, 60};
    const auto summary = axiom::core::summarizeScores(scores);
    EXPECT_EQ(summary.m_samples, 3);
    EXPECT_EQ(summary.m_minimum, 40);
    EXPECT_EQ(summary.m_maximum, 80);
    EXPECT_DOUBLE_EQ(summary.m_average, 60.0);
    EXPECT_EQ(summary.m_grade, axiom::core::QualityGrade::Passing);
}

TEST(Core, SummarizeScoresBoundaryAverageOf90IsExcellent) {
    const std::array scores = {90};
    const auto summary = axiom::core::summarizeScores(scores);
    EXPECT_EQ(summary.m_samples, 1);
    EXPECT_EQ(summary.m_minimum, 90);
    EXPECT_EQ(summary.m_maximum, 90);
    EXPECT_DOUBLE_EQ(summary.m_average, 90.0);
    EXPECT_EQ(summary.m_grade, axiom::core::QualityGrade::Excellent);
}
