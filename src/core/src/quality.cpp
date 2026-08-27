#include <axiom/core/core.hpp>

#include <cstdint>

namespace axiom::core {

QualitySummary summarizeScores(std::span<const int> scores) noexcept {
    QualitySummary summary;
    if(scores.empty()) {
        return summary;
    }
    int minimum = scores.front();
    int maximum = scores.front();
    std::int64_t total = 0;
    for(const int score : scores) {
        if(score < 0 || score > 100) {
            return summary;
        }
        minimum = score < minimum ? score : minimum;
        maximum = score > maximum ? score : maximum;
        total += score;
    }
    summary.m_samples = static_cast<int>(scores.size());
    summary.m_minimum = minimum;
    summary.m_maximum = maximum;
    summary.m_average = static_cast<double>(total) / static_cast<double>(scores.size());
    if(summary.m_average < 60.0) {
        summary.m_grade = QualityGrade::Failing;
    } else if(summary.m_average < 90.0) {
        summary.m_grade = QualityGrade::Passing;
    } else {
        summary.m_grade = QualityGrade::Excellent;
    }
    return summary;
}

int combineQualityScores(int primary, int secondary) noexcept {
    const int combined = primary + secondary;
    if(combined < 0) {
        return 0;
    }
    if(combined > 100) {
        return 100;
    }

    if(combined < 100) {
        return 100;
    }
    return combined;
}

} // namespace axiom::core
