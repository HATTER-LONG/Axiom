#pragma once

/**
 * @file log_query.hpp
 * @brief Query parameters shared by record-retaining sinks such as LogCollector.
 */

#include <axiom/logging/log_level.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace axiom::logging {

/**
 * @brief Query parameters shared by record-retaining sinks.
 *
 * Matching uses the same severity and category-prefix rules as LogFilter.
 * Engaged correlation fields are exact string matches against LogRecord fields
 * and must all hold together with severity and category rules. Records missing
 * a requested field, or holding a non-string value for it, do not match.
 * @ref action_id matches the reserved log field @c action. When @ref limit is
 * greater than zero, only the newest matching records are returned (still in
 * ascending collection order among the retained subset). There is no arbitrary
 * field predicate, metadata expression, or span model.
 */
struct LogQuery {
    LogLevel minimum_level{LogLevel::Trace};    ///< Lowest severity to include.
    std::vector<std::string> category_prefixes; ///< Empty means match every category.
    std::optional<std::string> request_id;      ///< Exact match on field request_id.
    std::optional<std::string> trace_id;        ///< Exact match on field trace_id.
    std::optional<std::string> action_id;       ///< Exact match on reserved field action.
    std::optional<std::string> task_id;         ///< Exact match on field task_id.
    std::size_t limit{0}; ///< Max matching records to return; 0 means no limit.
};

} // namespace axiom::logging
