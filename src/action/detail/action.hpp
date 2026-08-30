#pragma once

#include <axiom/action/invocation_context.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>

namespace axiom::detail {

#ifndef AXIOM_DETAIL_IACTION_DEFINED
#define AXIOM_DETAIL_IACTION_DEFINED
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
     *         Dispatcher converts them at the invocation boundary.
     */
    [[nodiscard]] virtual Result<Value> invoke(const Arguments& arguments,
                                               const InvocationContext& context) = 0;
};
#endif

} // namespace axiom::detail
