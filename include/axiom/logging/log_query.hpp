#pragma once

/**
 * @file log_query.hpp
 * @brief Query parameters shared by record-retaining sinks such as LogCollector.
 */

#include <axiom/logging/log_level.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace axiom::logging {

/**
 * @brief Query parameters shared by record-retaining sinks.
 *
 * Matching uses the same severity and category-prefix rules as LogFilter.
 * When @ref limit is greater than zero, only the newest matching records are
 * returned (still in ascending collection order among the retained subset).
 */
struct LogQuery {
    LogLevel minimum_level{LogLevel::Trace};    ///< Lowest severity to include.
    std::vector<std::string> category_prefixes; ///< Empty means match every category.
    std::size_t limit{0}; ///< Max matching records to return; 0 means no limit.
};

} // namespace axiom::logging
