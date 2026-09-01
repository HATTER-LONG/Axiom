#pragma once

/**
 * @file introspection_query.hpp
 * @brief Public value filters for IntrospectionService discovery lists.
 */

#include <axiom/task/task_types.hpp>

#include <optional>
#include <string>
#include <vector>

namespace axiom::introspection {

/**
 * @brief Conjunction of Action discovery filters.
 *
 * Every engaged condition must hold. Matching uses public ActionDescriptor
 * fields only. Empty @ref tags does not filter; duplicate query tags do not
 * change which Actions match. Results keep the source list order.
 */
struct ActionQuery final {
    /**
     * @brief Optional exact match against ActionId::module().
     *
     * Comparison is case-sensitive and requires the full module component. An
     * omitted value does not filter. A non-canonical or unknown module yields
     * an empty vector rather than an error.
     */
    std::optional<std::string> module;
    /**
     * @brief Labels the Action must contain (all-of, case-sensitive, exact).
     *
     * Tag order in this query is insignificant. An empty vector means no tag
     * constraint.
     */
    std::vector<std::string> tags;
};

/**
 * @brief Conjunction of Resource discovery filters.
 *
 * An engaged @ref type reuses Resource canonical-name validation. Illegal type
 * text is InvalidArgument; a legal type with no registrations is a successful
 * empty vector. Results keep the source list order.
 */
struct ResourceQuery final {
    /**
     * @brief Optional exact match against ResourceDescriptor::type.
     *
     * An omitted value does not filter.
     */
    std::optional<std::string> type;
};

/**
 * @brief Conjunction of Task discovery filters.
 *
 * Origin conditions require a TaskOrigin whose corresponding field equals the
 * query text exactly. Unknown association yields an empty vector; the query
 * never consults Action Runtime or logs. Results keep the source list order.
 */
struct TaskQuery final {
    /** @brief Optional exact match against TaskDescriptor::state. */
    std::optional<task::TaskState> state;
    /**
     * @brief Optional exact match against TaskOrigin::action_id.
     *
     * The Task must have an origin. This is canonical Action text, not ActionId.
     */
    std::optional<std::string> origin_action_id;
    /** @brief Optional exact match against TaskOrigin::request_id. */
    std::optional<std::string> origin_request_id;
};

} // namespace axiom::introspection
