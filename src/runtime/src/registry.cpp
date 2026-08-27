#include <axiom/runtime/detail/registry.hpp>

#include <algorithm>
#include <compare>
#include <iterator>
#include <utility>

#include <axiom/runtime/action.hpp>
#include <axiom/runtime/action_id.hpp>
#include <axiom/runtime/descriptor.hpp>
#include <axiom/runtime/result.hpp>
#include <axiom/runtime/value.hpp>

namespace axiom::runtime::detail {

Registry::~Registry() = default;

static bool idLess(const ActionId& lhs, const ActionId& rhs) {
    return lhs.toString() < rhs.toString();
}

Result<void> Registry::registerAction(const ActionId& id, std::unique_ptr<IAction> action) {
    if(!id.isValid()) {
        return Result<void>{Error{ErrorCode::InvalidArgument,
                                  "Invalid action id '" + id.toString() + "'", id.toString(),
                                  Value{}}};
    }
    if(action == nullptr) {
        return Result<void>{Error{ErrorCode::InvalidArgument,
                                  "Action '" + id.toString() + "' must not be null", id.toString(),
                                  Value{}}};
    }
    if(m_actions.contains(id.toString())) {
        return Result<void>{Error{ErrorCode::InvalidArgument,
                                  "Action '" + id.toString() + "' is already registered",
                                  id.toString(), Value{}}};
    }
    m_actions.emplace(id.toString(), std::move(action));
    return Result<void>{};
}

IAction* Registry::find(const ActionId& id) noexcept {
    const auto action = m_actions.find(id.toString());
    if(action == m_actions.end()) {
        return nullptr;
    }
    return action->second.get();
}

const IAction* Registry::find(const ActionId& id) const noexcept {
    const auto action = m_actions.find(id.toString());
    if(action == m_actions.end()) {
        return nullptr;
    }
    return action->second.get();
}

const ActionDescriptor* Registry::describe(const ActionId& id) const noexcept {
    const IAction* action = find(id);
    if(action == nullptr) {
        return nullptr;
    }
    return &action->descriptor();
}

std::vector<ActionId> Registry::listActions() const {
    std::vector<ActionId> ids;
    ids.reserve(m_actions.size());
    std::ranges::transform(m_actions, std::back_inserter(ids),
                           [](const auto& entry) { return ActionId{entry.first}; });
    std::ranges::sort(ids, idLess);
    return ids;
}

std::vector<ActionId> Registry::listActions(std::string_view module) const {
    std::vector<ActionId> ids;
    for(const auto& entry : m_actions) {
        const ActionId parsed{entry.first};
        if(parsed.module() == module) {
            ids.push_back(parsed);
        }
    }
    std::ranges::sort(ids, idLess);
    return ids;
}

Result<void> Registry::registerModule(const ModuleDescriptor& descriptor) {
    if(m_modules.contains(descriptor.m_name)) {
        return Result<void>{Error{ErrorCode::InvalidArgument,
                                  "Module '" + descriptor.m_name + "' is already registered",
                                  descriptor.m_name, Value{}}};
    }
    m_modules.emplace(descriptor.m_name, descriptor);
    return Result<void>{};
}

const ModuleDescriptor* Registry::findModule(std::string_view name) const noexcept {
    const auto module = m_modules.find(std::string{name});
    if(module == m_modules.end()) {
        return nullptr;
    }
    return &module->second;
}

} // namespace axiom::runtime::detail
