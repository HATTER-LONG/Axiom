#include "dispatcher.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <axiom/runtime/action_id.hpp>
#include <axiom/runtime/descriptor.hpp>
#include <axiom/runtime/detail/registry.hpp>
#include <axiom/runtime/module.hpp>
#include <axiom/runtime/result.hpp>
#include <axiom/runtime/runtime.hpp>
#include <axiom/runtime/value.hpp>

namespace axiom::runtime {

struct InvocationContext;

struct Runtime::Impl {
    detail::Registry m_registry;
    detail::Dispatcher m_dispatcher{m_registry};
};

Runtime::Runtime() : m_impl{std::make_unique<Impl>()} {}

Runtime::~Runtime() = default;

ModuleBuilder Runtime::module(std::string name, std::string description) {
    const ModuleDescriptor descriptor{.m_name = name, .m_description = std::move(description)};
    // Duplicate module registration is intentionally ignored so module() stays idempotent.
    (void)m_impl->m_registry.registerModule(descriptor);
    return ModuleBuilder{std::move(name), m_impl->m_registry};
}

const ActionDescriptor* Runtime::describe(std::string_view action_id) const noexcept {
    const ActionId id{action_id};
    if(!id.isValid()) {
        return nullptr;
    }
    return m_impl->m_registry.describe(id);
}

Result<Value> Runtime::invoke(std::string_view action_id,
                              const Arguments& arguments,
                              const InvocationContext& context) {
    const ActionId id{action_id};
    if(!id.isValid()) {
        return Result<Value>{Error{ErrorCode::InvalidArgument,
                                   "Invalid action id '" + std::string{action_id} + "'",
                                   std::string{action_id}, Value{}}};
    }
    return m_impl->m_dispatcher.invoke(id, arguments, context);
}

std::vector<ActionId> Runtime::listActions() const { return m_impl->m_registry.listActions(); }

std::vector<ActionId> Runtime::listActions(std::string_view module_name) const {
    return m_impl->m_registry.listActions(module_name);
}

} // namespace axiom::runtime
