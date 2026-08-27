#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <axiom/runtime/action_id.hpp>
#include <axiom/runtime/descriptor.hpp>
#include <axiom/runtime/invocation_context.hpp>
#include <axiom/runtime/module.hpp>
#include <axiom/runtime/result.hpp>
#include <axiom/runtime/value.hpp>

namespace axiom::runtime {

/**
 * @brief Public runtime facade owning the action registry and dispatch pipeline.
 *
 * Thread model: registration must complete before concurrent invocations begin;
 * concurrent invocations of registered actions are safe. Exceptions raised by
 * actions never escape invoke(); they are reported through Result.
 */
class Runtime {
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /**
     * @brief Registers module metadata and returns a builder for its actions.
     *
     * Calling module() again with the same name is allowed: the duplicate module
     * descriptor is intentionally ignored so module discovery stays idempotent.
     */
    ModuleBuilder module(std::string name, std::string description = {});

    /** @brief Returns the descriptor of the action, or nullptr for malformed/unknown ids. */
    [[nodiscard]] const ActionDescriptor* describe(std::string_view action_id) const noexcept;

    /** @brief Invokes the named action; failures are reported through Result. */
    Result<Value> invoke(std::string_view action_id,
                         const Arguments& arguments = {},
                         const InvocationContext& context = {});

    /** @brief Returns all registered action ids sorted by their string form. */
    [[nodiscard]] std::vector<ActionId> listActions() const;
    /** @brief Returns the module's registered action ids sorted by their string form. */
    [[nodiscard]] std::vector<ActionId> listActions(std::string_view module_name) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace axiom::runtime
