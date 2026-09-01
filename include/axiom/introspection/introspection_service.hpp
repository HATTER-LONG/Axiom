#pragma once

/**
 * @file introspection_service.hpp
 * @brief Non-owning read-only aggregation of Axiom runtime discovery.
 */

#include <axiom/action/action_id.hpp>
#include <axiom/action/descriptor.hpp>
#include <axiom/action/module.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/export.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/introspection/introspection_query.hpp>
#include <axiom/introspection/runtime_snapshot.hpp>
#include <axiom/resource/resource_descriptor.hpp>
#include <axiom/resource/resource_id.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_registry.hpp>
#include <axiom/task/task_types.hpp>

#include <string_view>
#include <vector>

namespace axiom::introspection {

/**
 * @brief Provides owning, uncached read-only discovery values.
 *
 * The service stores no registry state and never owns or modifies its three
 * sources. They must outlive this service, and their destruction must not
 * overlap a service query. Returned values are independent copies, so they do
 * not change when a source subsequently changes. Source queries may be used
 * concurrently according to the individual Runtime, ResourceRegistry, and
 * TaskRegistry contracts; a snapshot does not combine their states atomically.
 */
class AXIOM_API IntrospectionService final {
public:
    /**
     * @brief Binds the service to existing runtime registries without taking ownership.
     * @param actions Runtime owning Modules and Actions.
     * @param resources Registry owning Resource registrations.
     * @param tasks Registry retaining Task descriptors.
     * @pre Each source remains alive until this service is destroyed.
     */
    IntrospectionService(const Runtime& actions,
                         const resource::ResourceRegistry& resources,
                         const task::TaskRegistry& tasks) noexcept;
    /** @brief Destroys the non-owning service without affecting its sources. */
    ~IntrospectionService() noexcept = default;

    IntrospectionService(const IntrospectionService&) = delete;
    IntrospectionService& operator=(const IntrospectionService&) = delete;
    IntrospectionService(IntrospectionService&&) = delete;
    IntrospectionService& operator=(IntrospectionService&&) = delete;

    /**
     * @brief Returns independently owned Modules sorted by canonical namespace.
     *
     * @return Owning value copies in ascending canonical namespace order.
     * @throws
     * std::bad_alloc If result allocation or descriptor copying fails.
     * @note May run
     * concurrently with source operations. This service and all three
     *       sources must
     * remain alive; source destruction must not overlap the query.
     */
    [[nodiscard]] std::vector<ModuleDescriptor> modules() const;
    /**
     * @brief Returns independently owned Actions sorted by canonical full identifier.

     * * @return Owning value copies in ascending canonical full-identifier order.
     * @throws
     * std::bad_alloc If result allocation or descriptor copying fails.
     * @note May run
     * concurrently with source operations; source lifetime rules apply.
     */
    [[nodiscard]] std::vector<ActionDescriptor> actions() const;
    /**
     * @brief Returns Actions whose module component exactly equals @p module_namespace.
     * @param module_namespace Canonical Module namespace; no prefix matching is performed.
     *
     * @return Matching Actions sorted by full identifier, or an empty vector.
     * @throws
     * std::bad_alloc If result allocation or descriptor copying fails.
     * @note May run
     * concurrently with source operations; source lifetime rules apply.
     */
    [[nodiscard]] std::vector<ActionDescriptor> actions(std::string_view module_namespace) const;
    /**
     * @brief Returns Actions matching every engaged condition in @p query.
     * @param query Module and tag filters combined with AND; empty conditions do not
     *        filter. Tag matching is all-of. Duplicate query tags do not change
     *        which Actions match. An unknown module yields an empty vector.
     * @return Matching Actions in the same order as actions().
     * @throws std::bad_alloc If result allocation or descriptor copying fails.
     * @note May run concurrently with source operations; source lifetime rules apply.
     *       Filtering copies one discoverActions snapshot; this service does not
     *       index or cache registry state.
     */
    [[nodiscard]] std::vector<ActionDescriptor> actions(const ActionQuery& query) const;
    /**
     * @brief Returns an owning copy of one Action description.
     * @param id Parsed canonical Action identifier.
     * @return Deep-copied description, or the
     * source's NotFound error unchanged.
     * @throws std::bad_alloc If descriptor copying or
     * result/error allocation fails.
     * @note May run concurrently with source operations;
     * source lifetime rules apply.
     */
    [[nodiscard]] Result<ActionDescriptor> describeAction(const ActionId& id) const;

    /**
     * @brief Returns independently owned Resources sorted by full identifier.
     *
     * @return Owning value copies in ascending canonical resource-identifier order.
     * @throws
     * std::bad_alloc If result allocation or descriptor copying fails.
     * @note May run
     * concurrently with source operations; source lifetime rules apply.
     */
    [[nodiscard]] std::vector<resource::ResourceDescriptor> resources() const;
    /**
     * @brief Returns Resources whose logical type exactly equals @p type.
     * @param type Canonical logical Resource type (`[a-z][a-z0-9_]*`).
     * @return Matching Resources sorted by full identifier, or InvalidArgument for a
     *
     * non-canonical type.
     * @throws std::bad_alloc If result allocation, validation, or
     * descriptor copying fails.
     * @note May run concurrently with source operations; source
     * lifetime rules apply.
     */
    [[nodiscard]] Result<std::vector<resource::ResourceDescriptor>>
    resources(std::string_view type) const;
    /**
     * @brief Returns Resources matching every engaged condition in @p query.
     * @param query Type filter; an omitted type does not filter. Illegal type
     *        text returns InvalidArgument using the same validation as
     *        resources(std::string_view).
     * @return Matching Resources in the same order as resources(), or InvalidArgument.
     * @throws std::bad_alloc If result allocation, validation, or descriptor copying
     *         fails.
     * @note May run concurrently with source operations; source lifetime rules apply.
     *       Filtering copies one ResourceRegistry::list() result.
     */
    [[nodiscard]] Result<std::vector<resource::ResourceDescriptor>>
    resources(const ResourceQuery& query) const;
    /**
     * @brief Returns an owning copy of one Resource description.
     * @param id Resource identity to query.
     * @return Description, or the source's NotFound
     * error unchanged.
     * @throws std::bad_alloc If descriptor copying or result/error
     * allocation fails.
     * @note May run concurrently with source operations; source lifetime
     * rules apply.
     */
    [[nodiscard]] Result<resource::ResourceDescriptor>
    describeResource(const resource::ResourceId& id) const;

    /**
     * @brief Returns independently owned Task descriptors sorted by canonical ID.
     *
     * @return Owning value copies in ascending canonical task-ID order.
     * @throws
     * std::bad_alloc If result allocation or descriptor copying fails.
     * @note May run
     * concurrently with source operations; source lifetime rules apply.
     */
    [[nodiscard]] std::vector<task::TaskDescriptor> tasks() const;
    /**
     * @brief Returns Tasks matching every engaged condition in @p query.
     * @param query State and origin filters combined with AND. Origin conditions
     *        require a TaskOrigin whose corresponding field matches exactly;
     *        unknown association yields an empty vector.
     * @return Matching Tasks in the same order as tasks().
     * @throws std::bad_alloc If result allocation or descriptor copying fails.
     * @note May run concurrently with source operations; source lifetime rules apply.
     *       Filtering copies one TaskRegistry::list() result and does not call
     *       describe() per Task.
     */
    [[nodiscard]] std::vector<task::TaskDescriptor> tasks(const TaskQuery& query) const;
    /**
     * @brief Returns an owning copy of one Task description.
     * @param id Task identity to query.
     * @return Description, or the source's NotFound error
     * unchanged.
     * @throws std::bad_alloc If descriptor copying or result/error allocation
     * fails.
     * @note May run concurrently with source operations; source lifetime rules
     * apply.
     */
    [[nodiscard]] Result<task::TaskDescriptor> describeTask(const task::TaskId& id) const;

    /**
     * @brief Collects modules/actions, then resources, then tasks in that fixed order.
     *
     * @return An independently owned sequential observation; it is not globally atomic.
     *         The snapshot always contains every discoverable object and does not
     *         accept a Query.
     *
     * @throws std::bad_alloc If collection or any descriptor copy fails.
     * @note May run
     * concurrently with source operations. It samples each source in the
     *       stated order,
     * so concurrent changes can be reflected by different source
     *       generations. Source
     * destruction must not overlap this query.
     */
    [[nodiscard]] RuntimeSnapshot snapshot() const;

private:
    const Runtime* actions_;
    const resource::ResourceRegistry* resources_;
    const task::TaskRegistry* tasks_;
};

} // namespace axiom::introspection
