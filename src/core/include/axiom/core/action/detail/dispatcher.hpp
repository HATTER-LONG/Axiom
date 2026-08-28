#pragma once

#include <axiom/core/action/action_id.hpp>
#include <axiom/core/action/detail/registry.hpp>
#include <axiom/core/action/invocation_context.hpp>
#include <axiom/core/base/result.hpp>
#include <axiom/core/base/value.hpp>

namespace axiom::core::detail {

/**
 * @brief Resolves and synchronously invokes registered Actions.
 *
 * Dispatcher is the sole exception-normalization boundary for dynamic Action
 * calls. It validates argument names and required arguments before delegating
 * to an IAction; Value conversion remains the responsibility of a later
 * callable adapter rather than Registry or Dispatcher.
 */
class Dispatcher {
public:
    /**
     * @brief Binds a Dispatcher to a Registry whose lifetime must outlive it.
     *
     * @param registry Registered Action source to resolve during invocation.
     */
    explicit Dispatcher(Registry& registry) noexcept : registry(registry) {}

    /**
     * @brief Looks up, validates, and invokes an Action.
     *
     * @param id Parsed Action identifier to invoke.
     * @param arguments Named dynamic arguments.
     * @param context Diagnostic context forwarded unchanged to the Action.
     * @return The Action's result, a structural argument error, NotFound, or an
     *         exception-normalization error. Business Errors are preserved.
     */
    [[nodiscard]] Result<Value>
    invoke(const ActionId& id, const Arguments& arguments, const InvocationContext& context) const;

private:
    Registry& registry;
};

} // namespace axiom::core::detail
