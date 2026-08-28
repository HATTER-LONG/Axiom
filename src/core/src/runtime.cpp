#include <axiom/core/action/runtime.hpp>

#include <axiom/core/action/action_id.hpp>
#include <axiom/core/action/descriptor.hpp>
#include <axiom/core/action/detail/dispatcher.hpp>
#include <axiom/core/action/detail/module_builder_state.hpp>
#include <axiom/core/action/detail/registry.hpp>
#include <axiom/core/action/invocation_context.hpp>
#include <axiom/core/action/module.hpp>
#include <axiom/core/action/module_builder.hpp>
#include <axiom/core/base/error.hpp>
#include <axiom/core/base/result.hpp>
#include <axiom/core/base/value.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace axiom::core::detail {

class RuntimeState {
public:
    Registry registry;
    Dispatcher dispatcher{registry};
};

} // namespace axiom::core::detail

namespace axiom::core {

Runtime::Runtime() : state(std::make_unique<detail::RuntimeState>()) {}
Runtime::~Runtime() noexcept = default;

Result<void> Runtime::registerModule(ModuleBuilder&& builder) {
    if(!builder.state) {
        return Result<void>::failure({.code = ErrorCode::InvalidArgument,
                                      .message = "ModuleBuilder must not be empty",
                                      .path = std::nullopt,
                                      .details = std::nullopt});
    }
    auto result = state->registry.registerModuleWithActions(builder.state->descriptor,
                                                            builder.state->actions);
    if(result) {
        builder.state.reset();
    }
    return result;
}

Result<Value> Runtime::invoke(const ActionId& id,
                              const Arguments& arguments,
                              const InvocationContext& context) const {
    return state->dispatcher.invoke(id, arguments, context);
}

Result<std::reference_wrapper<const ModuleDescriptor>>
Runtime::findModule(const std::string_view namespace_name) const {
    return state->registry.findModule(namespace_name);
}

Result<std::reference_wrapper<const ActionDescriptor>>
Runtime::findAction(const ActionId& id) const {
    return state->registry.findAction(id);
}

std::vector<std::reference_wrapper<const ModuleDescriptor>> Runtime::discoverModules() const {
    return state->registry.discoverModules();
}

std::vector<std::reference_wrapper<const ActionDescriptor>> Runtime::discoverActions() const {
    return state->registry.discoverActions();
}

} // namespace axiom::core
