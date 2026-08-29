#pragma once

#include <axiom/core/logging/log_level.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace axiom::core::logging {

/**
 * @brief Selects records for a sink by severity and category-prefix segments.
 *
 * A prefix `runtime` matches `runtime` and `runtime.action`, but never `runtime2`.
 */
struct LogFilter {
    LogLevel minimum_level{LogLevel::Trace};
    std::vector<std::string> category_prefixes;

    /** @brief Returns whether the supplied severity and category match this filter. */
    [[nodiscard]] bool matches(LogLevel level, std::string_view category) const noexcept;
};

} // namespace axiom::core::logging
