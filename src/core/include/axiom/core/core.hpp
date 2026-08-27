#pragma once

/**
 * @file core.hpp
 * @brief Convenience header for the Axiom Core API.
 */

#include <span>

namespace axiom::core {

/** @brief Returns the framework name. */
const char* frameworkName() noexcept;
const char* frameworkName2() noexcept;
/** @brief Quality classification derived from a set of scores. */
enum class QualityGrade { Invalid, Failing, Passing, Excellent };

/** @brief Aggregate statistics over a set of scores between 0 and 100. */
struct QualitySummary {
    int m_samples = 0;
    int m_minimum = 0;
    int m_maximum = 0;
    double m_average = 0.0;
    QualityGrade m_grade = QualityGrade::Invalid;
};

/** @brief Summarizes scores; an empty or out-of-range input is Invalid. */
QualitySummary summarizeScores(std::span<const int> scores) noexcept;

/** @brief Adds two score contributions and clamps the result to the inclusive range [0, 100]. */
int combineQualityScores(int primary, int secondary) noexcept;

} // namespace axiom::core
