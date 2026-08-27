#pragma once

#include <axiom/runtime/descriptor.hpp>
#include <axiom/runtime/invocation_context.hpp>
#include <axiom/runtime/result.hpp>
#include <axiom/runtime/value.hpp>

namespace axiom::runtime {

/**
 * @brief Stable runtime ABI: every typed action adapts to this interface.
 *
 * Implementations must not let exceptions escape invoke(); failures are
 * reported through Result<Value>.
 */
class IAction {
public:
    virtual ~IAction() = default;

    /** @brief Returns the self-description of this action. */
    [[nodiscard]] virtual const ActionDescriptor& descriptor() const noexcept = 0;

    /** @brief Invokes the action with named arguments and invocation metadata. */
    virtual Result<Value> invoke(const Arguments& arguments, const InvocationContext& context) = 0;
};

} // namespace axiom::runtime
