#pragma once

/**
 * @file command_dispatcher.hpp
 * @brief Protocol-independent Command routing over Runtime, Introspection, and Task.
 */

#include <axiom/action/invocation_context.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/export.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/task/task_registry.hpp>

#include <memory>
#include <string_view>

namespace axiom::command {

/**
 * @brief Closed, protocol-independent dynamic Command boundary.
 *
 * CommandDispatcher validates Command structure, routes known methods, and
 * converts public descriptors into owned Values. Runtime, IntrospectionService,
 * ResourceRegistry, and TaskRegistry remain the authorities for business
 * semantics. This type does not own those sources, hold a Command-layer lock
 * around user Actions, or promise a globally atomic snapshot. Routing is
 * exhaustive over the closed method set; unknown methods are rejected rather
 * than mapped onto another command.
 *
 * @pre The Runtime, ResourceRegistry, and TaskRegistry passed to the constructor
 *      remain alive until this dispatcher is destroyed. Source destruction must
 *      not overlap `dispatch()`.
 * @note Distinct threads may call `dispatch()` concurrently on one dispatcher.
 *       Concrete thread-safety follows the existing contracts of the three
 *       sources. `system.snapshot` remains a sequential observation.
 */
class AXIOM_API CommandDispatcher final {
public:
    /**
     * @brief Binds the dispatcher to existing sources and constructs an internal
     *        IntrospectionService over them.
     *
     * @param runtime Runtime that owns Modules and Actions.
     * @param resources Registry that owns Resource registrations.
     * @param tasks Registry that retains Task descriptors and accepts cancel.
     * @throws std::bad_alloc If dispatcher state cannot be allocated.
     * @pre Each source remains alive until this dispatcher is destroyed.
     */
    CommandDispatcher(const Runtime& runtime,
                      const resource::ResourceRegistry& resources,
                      task::TaskRegistry& tasks);
    /**
     * @brief Destroys the dispatcher without affecting its sources.
     */
    ~CommandDispatcher() noexcept;

    CommandDispatcher(const CommandDispatcher&) = delete;
    CommandDispatcher& operator=(const CommandDispatcher&) = delete;
    CommandDispatcher(CommandDispatcher&&) = delete;
    CommandDispatcher& operator=(CommandDispatcher&&) = delete;

    /**
     * @brief Validates, routes, and executes one Command.
     *
     * @param method Stable Command method name.
     * @param params Strict Command parameter object.
     * @param context Diagnostic context forwarded unchanged to `Runtime::invoke()`.
     * @return An owned success Value, a Command structure error, or a source error.
     *         Failures stay in `Result`; they are never wrapped as a success Value.
     * @throws std::bad_alloc If query copying or Value allocation fails.
     * @note Unknown methods return `ErrorCode::UnknownCommand`. Missing, unknown,
     *       and mistyped Command fields are reported before any source call.
     *       Source `NotFound` and business Errors are preserved. Runtime argument
     *       paths stay relative to the Action `arguments` object.
     */
    [[nodiscard]] Result<Value> dispatch(std::string_view method,
                                         const Value::Object& params,
                                         const InvocationContext& context = {}) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace axiom::command
