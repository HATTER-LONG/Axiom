#pragma once

/**
 * @file log_filter.hpp
 * @brief Per-sink severity and category-prefix selection for LoggingService.
 */

#include <axiom/export.hpp>
#include <axiom/logging/log_level.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace axiom::logging {

/**
 * @brief Selects records for a sink by severity and category-prefix segments.
 *
 * A prefix `runtime` matches `runtime` and `runtime.action`, but never `runtime2`
 * (matching requires an exact segment boundary at the end of the prefix).
 */
struct LogFilter {
    LogLevel minimum_level{LogLevel::Trace};    ///< Lowest severity accepted by the sink.
    std::vector<std::string> category_prefixes; ///< Empty means accept every category.

    /**
     * @brief Returns whether the supplied severity and category match this filter.
     * @param level Severity of the candidate record.
     * @param category Dot-separated category path of the candidate record.
     * @return true when severity and category both satisfy this filter.
     */
    [[nodiscard]] AXIOM_API bool matches(LogLevel level, std::string_view category) const noexcept;
};

} // namespace axiom::logging
