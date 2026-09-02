#pragma once

/** @file action.hpp
 * @brief Internal callable boundary; not a supported extension API.
 */

#include <axiom/action/action_id.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>

namespace axiom::detail {

/**
 * @brief Internal polymorphic implementation of a registered Action.
 *
 * Registry owns implementations through this interface. It is an in-process
 * extension point only and does not promise binary compatibility across
 * compilers, Axiom versions, or shared-library boundaries.
 */
class IAction {
public:
    /** @brief Destroys an Action through its internal base interface. */
    virtual ~IAction() noexcept = default;

    /**
     * @brief Invokes the Action with structurally validated named arguments.
     *
     * @param arguments Named Values supplied by the caller.
     * @param context Diagnostic context preserved without Axiom policy decisions.
     * @return A dynamic result or an expected business Error.
     * @throws std::exception Implementations may signal unexpected failures; the
     *         ActionInvoker converts them at the invocation boundary.
     */
    [[nodiscard]] virtual Result<Value> invoke(const Arguments& arguments,
                                               const InvocationContext& context) = 0;

    /**
     * @brief Receives the canonical ActionId after the builder accepts this registration.
     * @param id Identifier copied from the validated ActionDescriptor.
     */
    virtual void bindActionId(const ActionId& id) { static_cast<void>(id); }
};

} // namespace axiom::detail
