#pragma once

/**
 * @file invocation_context.hpp
 * @brief Caller-owned diagnostic context for action invocations.
 */

#include <functional>
#include <map>
#include <string>

namespace axiom {

/**
 * @brief Diagnostic information associated with one Action invocation.
 *
 * Axiom preserves this information for Action implementations and diagnostic
 * adapters, but never interprets caller identity to select or alter business
 * behavior. Metadata keys retain lexical order for reproducible diagnostics.
 */
struct InvocationContext {
    /** @brief Host-assigned identifier for this request, when available. */
    std::string request_id;
    /** @brief Host-assigned tracing identifier, when available. */
    std::string trace_id;
    /** @brief Diagnostic identity of the invoking caller, when available. */
    std::string caller;
    /** @brief Additional host-provided diagnostic metadata. */
    std::map<std::string, std::string, std::less<>> metadata;
};

} // namespace axiom
