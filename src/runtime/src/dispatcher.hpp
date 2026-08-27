#pragma once

#include <axiom/runtime/result.hpp>
#include <axiom/runtime/value.hpp>

namespace axiom::runtime {

class ActionId;
struct InvocationContext;

} // namespace axiom::runtime

namespace axiom::runtime::detail {

class Registry;

/**
 * @brief Invocation pipeline: resolve, validate presence, delegate, wrap exceptions.
 *
 * Resolution failures map to ActionNotFound, missing required parameters to
 * MissingArgument (path = parameter name), and arguments not declared by the
 * action's descriptor to InvalidArgument (path = argument name). Type
 * conversion is left to the action's adapter; the dispatcher only provides the
 * second exception boundary around IAction::invoke.
 */
class Dispatcher {
public:
    explicit Dispatcher(Registry& registry);

    /** @brief Invokes the action registered under the id with the given arguments. */
    Result<Value>
    invoke(const ActionId& id, const Arguments& arguments, const InvocationContext& context);

private:
    Registry& m_registry;
};

} // namespace axiom::runtime::detail
