#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <axiom/runtime/action_id.hpp>
#include <axiom/runtime/descriptor.hpp>
#include <axiom/runtime/result.hpp>

namespace axiom::runtime {

class IAction;

} // namespace axiom::runtime

namespace axiom::runtime::detail {

/**
 * @brief Registration and discovery store for actions and module metadata.
 *
 * Owns every registered action through unique_ptr and detects registration
 * conflicts. Not responsible for parameter conversion, action execution, or
 * error mapping beyond registration conflicts.
 */
class Registry {
public:
    /** @brief Defined in registry.cpp where the complete IAction type is available. */
    ~Registry();

    /** @brief Registers an action under the given id; rejects invalid ids and duplicates. */
    Result<void> registerAction(const ActionId& id, std::unique_ptr<IAction> action);

    /** @brief Returns the action registered under the id, or nullptr if absent. */
    [[nodiscard]] IAction* find(const ActionId& id) noexcept;
    /** @brief Returns the action registered under the id, or nullptr if absent. */
    [[nodiscard]] const IAction* find(const ActionId& id) const noexcept;

    /** @brief Returns the descriptor of the action registered under the id, or nullptr. */
    [[nodiscard]] const ActionDescriptor* describe(const ActionId& id) const noexcept;

    /** @brief Returns all registered action ids sorted by their string form. */
    [[nodiscard]] std::vector<ActionId> listActions() const;
    /** @brief Returns registered action ids of the given module sorted by their string form. */
    [[nodiscard]] std::vector<ActionId> listActions(std::string_view module) const;

    /** @brief Registers module metadata; rejects duplicate module names. */
    Result<void> registerModule(const ModuleDescriptor& descriptor);
    /** @brief Returns the metadata of the named module, or nullptr if absent. */
    [[nodiscard]] const ModuleDescriptor* findModule(std::string_view name) const noexcept;

private:
    std::unordered_map<std::string, std::unique_ptr<IAction>> m_actions;
    std::unordered_map<std::string, ModuleDescriptor> m_modules;
};

} // namespace axiom::runtime::detail
