#pragma once

#include <axiom/action/action_id.hpp>
#include <axiom/action/descriptor.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/action/module.hpp>
#include <axiom/export.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/logging/logger.hpp>

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace axiom {

class ModuleBuilder;
namespace detail {
class RuntimeState;
}

/**
 * @brief Owns synchronously invocable Modules and their Actions.
 *
 * Runtime coordinates concurrent registration, discovery, lookup, and invocation.
 * Registration commits are serialized and published atomically; discovery and lookup
 * observe immutable snapshots. Registering a ModuleBuilder validates its entire
 * pending contents before publication, so expected registration failures leave
 * Runtime unchanged. Runtime destruction and concurrent access to the same Runtime
 * object still require caller synchronization.
 */
class AXIOM_API Runtime {
public:
    /**
     * @brief Creates an empty synchronous Runtime.
     * @throws std::bad_alloc If Runtime state cannot be allocated.
     */
    Runtime();
    /**
     * @brief Creates an empty Runtime that emits side-channel records through @p logger.
     *
     * Runtime derives the `module` and `action` categories beneath @p logger. Logging
     * failures never change registration or invocation behavior.
     *
     * @param logger Logger associated with the LoggingService that receives Runtime records.
     * @throws std::bad_alloc If Runtime state cannot be allocated.
     */
    explicit Runtime(logging::Logger logger);
    /** @brief Destroys registered Action implementations. */
    ~Runtime() noexcept;

    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /**
     * @brief Validates and atomically registers a detached ModuleBuilder.
     * @param builder Pending module and actions whose ownership transfers on success.
     * @return Success or a validation/conflict error with no Runtime state change.
     */
    [[nodiscard]] Result<void> registerModule(ModuleBuilder&& builder);

    /**
     * @brief Invokes a registered Action synchronously.
     * @param id Parsed full Action identifier.
     * @param arguments Named dynamic invocation arguments.
     * @param context Diagnostic context forwarded unchanged to the callable.
     * @return Dynamic result, structural invocation error, preserved business Error, or
     * normalized exception error.
     */
    [[nodiscard]] Result<Value> invoke(const ActionId& id,
                                       const Arguments& arguments,
                                       const InvocationContext& context = {}) const;

    /**
     * @brief Finds a registered Module.
     * @param namespace_name Canonical module namespace to query.
     * @return A read-only descriptor reference or a NotFound error.
     * @note The reference remains valid until this Runtime is destroyed, including
     *       while other Modules are registered.
     */
    [[nodiscard]] Result<std::reference_wrapper<const ModuleDescriptor>>
    findModule(std::string_view namespace_name) const;
    /**
     * @brief Finds a registered Action descriptor.
     * @param id Parsed canonical Action identifier to query.
     * @return A read-only descriptor reference or a NotFound error.
     * @note The reference remains valid until this Runtime is destroyed, including
     *       while other Modules are registered; nested TypeDescriptor pointers are
     *       recursively const.
     */
    [[nodiscard]] Result<std::reference_wrapper<const ActionDescriptor>>
    findAction(const ActionId& id) const;
    /**
     * @brief Lists registered Modules in ascending namespace order.
     * @return Read-only descriptor references owned by this Runtime.
     * @note Every reference remains valid until this Runtime is destroyed, including
     *       while other Modules are registered.
     */
    [[nodiscard]] std::vector<std::reference_wrapper<const ModuleDescriptor>>
    discoverModules() const;
    /**
     * @brief Lists registered Actions in ascending full identifier order.
     * @return Read-only descriptor references owned by this Runtime.
     * @note Every reference remains valid until this Runtime is destroyed, including
     *       while other Modules are registered; nested TypeDescriptor pointers are
     *       recursively const.
     */
    [[nodiscard]] std::vector<std::reference_wrapper<const ActionDescriptor>>
    discoverActions() const;

private:
    std::unique_ptr<detail::RuntimeState> state_;
};

} // namespace axiom
