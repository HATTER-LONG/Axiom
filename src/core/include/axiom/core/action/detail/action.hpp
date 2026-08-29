#pragma once

#include <axiom/core/action/invocation_context.hpp>
#include <axiom/core/base/result.hpp>
#include <axiom/core/base/value.hpp>

namespace axiom::core::detail {

/**
 * @brief Internal polymorphic implementation of a registered Action.
 *
 * Registry owns implementations through this interface. It is an in-process
 * extension point only and does not promise binary compatibility across
 * compilers, Core versions, or shared-library boundaries.
 */
class IAction {
public:
    /** @brief Destroys an Action through its internal base interface. */
    virtual ~IAction() noexcept = default;

    /**
     * @brief Invokes the Action with structurally validated named arguments.
     *
     * @param arguments Named Values supplied by the caller.
     * @param context Diagnostic context preserved without Core policy decisions.
     * @return A dynamic result or an expected business Error.
     * @throws std::exception Implementations may signal unexpected failures; the
     *         Dispatcher converts them at the invocation boundary.
     */
    [[nodiscard]] virtual Result<Value> invoke(const Arguments& arguments,
                                               const InvocationContext& context) = 0;
};

} // namespace axiom::core::detail
