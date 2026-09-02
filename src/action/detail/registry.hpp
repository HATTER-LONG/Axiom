#pragma once

#include <axiom/action/action_id.hpp>
#include <axiom/action/descriptor.hpp>
#include <axiom/action/detail/action.hpp>
#include <axiom/action/module.hpp>
#include <axiom/foundation/result.hpp>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace axiom::detail {

class ActionInvoker;

/** @brief Staged Action ownership used to preserve all-or-nothing Module registration. */
struct PendingAction {
    /**
     * @brief Stages an Action descriptor and its owned implementation.
     * @param action_descriptor Owned descriptor to retain for atomic registration.
     * @param action_implementation Implementation whose ownership transfers to this stage.
     */
    PendingAction(std::unique_ptr<ActionDescriptor> action_descriptor,
                  std::unique_ptr<IAction> action_implementation)
        : descriptor(std::move(action_descriptor)),
          implementation(std::move(action_implementation)) {}

    /** @brief Releases the staged implementation without propagating an exception. */
    ~PendingAction() noexcept = default;
    PendingAction(PendingAction&&) noexcept = default;
    PendingAction& operator=(PendingAction&&) noexcept = default;
    PendingAction(const PendingAction&) = delete;
    PendingAction& operator=(const PendingAction&) = delete;

    std::unique_ptr<ActionDescriptor> descriptor;
    std::unique_ptr<IAction> implementation;
};

/**
 * @brief Owns registered Modules, Action descriptions, and Action implementations.
 *
 * Registry validates descriptions before mutation, rejects duplicate module and
 * Action identifiers, and provides deterministic discovery ordered by the canonical
 * module or Action identifier. It does not invoke implementations, convert Values,
 * or impose logging behavior. Registration commits are serialized, while queries
 * and Action resolution read immutable state snapshots and may run concurrently.
 */
class Registry {
public:
    /** @brief Creates an empty Registry. */
    Registry();
    /** @brief Destroys all registered Action implementations before their descriptions. */
    ~Registry() noexcept;

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    /**
     * @brief Validates and registers a Module description.
     *
     * @param descriptor Module description to copy into the Registry.
     * @return Success when the Module is registered; InvalidDescriptor for an invalid
     *         description, AlreadyExists for an existing namespace, or InternalError
     *         if a candidate state cannot be prepared.
     * @post On failure, no registered Module or Action is changed.
     */
    [[nodiscard]] Result<void> registerModule(const ModuleDescriptor& descriptor);

    /**
     * @brief Registers a Module and all of its prevalidated Actions as one logical operation.
     *
     * @param descriptor Module descriptor to own.
     * @param pending_actions Actions whose IDs must belong to descriptor.namespace_name.
     * @return Success or a structured registration error with no Registry or pending
     *         Action state change.
     * @post On success, every pending implementation is owned by Registry. On failure,
     *       all pending implementations remain owned by pending_actions.
     */
    [[nodiscard]] Result<void>
    registerModuleWithActions(const ModuleDescriptor& descriptor,
                              std::vector<PendingAction>& pending_actions);

    /**
     * @brief Validates and registers an Action implementation under an existing Module.
     *
     * @param descriptor Action description to copy into the Registry.
     * @param implementation Candidate implementation whose ownership transfers to this
     *        call. It is retained by Registry only when registration succeeds.
     * @return Success when the Action is registered; InvalidDescriptor for an invalid
     *         description, InvalidArgument for a null implementation, NotFound when
     *         the described Module is absent, AlreadyExists for a duplicate ID, or
     *         InternalError if a candidate state cannot be prepared.
     * @post On failure, no registered Module or Action is changed.
     */
    [[nodiscard]] Result<void> registerAction(const ActionDescriptor& descriptor,
                                              std::unique_ptr<IAction> implementation);

    /**
     * @brief Looks up a registered Module by namespace.
     *
     * @param namespace_name Canonical Module namespace to find.
     * @return A read-only descriptor view, or NotFound when the Module is absent.
     * @note The returned reference remains valid until this Registry is destroyed.
     */
    [[nodiscard]] Result<std::reference_wrapper<const ModuleDescriptor>>
    findModule(std::string_view namespace_name) const;

    /**
     * @brief Looks up a registered Action description by canonical identifier.
     *
     * @param id Parsed Action identifier to find.
     * @return A read-only descriptor view, or NotFound when the Action is absent.
     * @note The returned reference remains valid until this Registry is destroyed.
     */
    [[nodiscard]] Result<std::reference_wrapper<const ActionDescriptor>>
    findAction(const ActionId& id) const;

    /**
     * @brief Lists all Module descriptions in ascending namespace order.
     *
     * @return Read-only descriptor views owned by this Registry.
     * @note Returned references remain valid until this Registry is destroyed.
     */
    [[nodiscard]] std::vector<std::reference_wrapper<const ModuleDescriptor>>
    discoverModules() const;

    /**
     * @brief Lists all Action descriptions in ascending canonical identifier order.
     *
     * @return Read-only descriptor views owned by this Registry.
     * @note Returned references remain valid until this Registry is destroyed.
     */
    [[nodiscard]] std::vector<std::reference_wrapper<const ActionDescriptor>>
    discoverActions() const;

private:
    friend class ActionInvoker;

    /**
     * @brief Finds the internal implementation for an already parsed Action ID.
     *
     * @param id Parsed Action identifier to find.
     * @return The owned implementation, or NotFound when the Action is absent.
     * @note This is reserved for ActionInvoker so Registry never invokes Actions.
     */
    [[nodiscard]] Result<std::reference_wrapper<IAction>> findImplementation(const ActionId& id);

    struct RegisteredAction {
        std::unique_ptr<ActionDescriptor> descriptor;
        std::unique_ptr<IAction> implementation;

        ~RegisteredAction() noexcept = default;
    };

    /** @brief Immutable-at-commit ownership snapshot for atomic registrations. */
    struct State {
        std::map<std::string, std::shared_ptr<const ModuleDescriptor>, std::less<>> modules;
        std::map<std::string, std::shared_ptr<RegisteredAction>, std::less<>> actions;
    };

    // State is immutable after publication. Atomic shared_ptr operations let readers
    // retain a complete snapshot without holding a lock across descriptor access or
    // Action invocation. The mutex only serializes competing registration commits.
    std::atomic<std::shared_ptr<const State>> state_;
    mutable std::mutex registration_mutex_;
};

} // namespace axiom::detail
