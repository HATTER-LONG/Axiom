#pragma once

/**
 * @file invocation_context.hpp
 * @brief Caller-owned diagnostic context for action invocations.
 */

#include <axiom/action/action_id.hpp>

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

/**
 * @brief Call-stack view of the Action identity and diagnostic context for one invoke.
 *
 * Axiom injects this value into callables registered with ModuleBuilder::addContextual().
 * It is not an Action parameter and is valid only while the synchronous invoke call
 * remains on the stack. Copy request, trace, caller, metadata, or ActionId values before
 * returning if they must outlive the call.
 */
class ActionInvocation final {
public:
    /**
     * @brief Binds borrowed identity and context for the current synchronous invoke.
     * @param action_id Authoritative identifier of the registered Action.
     * @param context Diagnostic context supplied to Runtime::invoke().
     * @warning Both references must outlive this view.
     */
    ActionInvocation(const ActionId& action_id, const InvocationContext& context) noexcept
        : action_id_(action_id), context_(context) {}

    /**
     * @brief Returns the canonical ActionId for this registration.
     * @return Reference valid while this view and the registered Action remain alive.
     */
    [[nodiscard]] const ActionId& actionId() const noexcept { return action_id_; }

    /**
     * @brief Returns the diagnostic context for this invoke.
     * @return Reference valid only for the current synchronous call stack.
     */
    [[nodiscard]] const InvocationContext& context() const noexcept { return context_; }

private:
    const ActionId& action_id_;
    const InvocationContext& context_;
};

} // namespace axiom
