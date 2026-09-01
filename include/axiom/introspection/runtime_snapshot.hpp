#pragma once

/**
 * @file runtime_snapshot.hpp
 * @brief Owning value snapshot of the observable Axiom runtime.
 */

#include <axiom/action/descriptor.hpp>
#include <axiom/action/module.hpp>
#include <axiom/resource/resource_descriptor.hpp>
#include <axiom/task/task_types.hpp>

#include <vector>

namespace axiom::introspection {

/**
 * @brief Values collected in order from the three runtime discovery sources.
 *
 * Every member is independently owned and remains unchanged after the source
 * registries change. The members are collected in module/action, resource,
 * then task order; this is a sequential observation, not a cross-registry
 * atomic snapshot. Each source retains its own list consistency contract.
 */
struct RuntimeSnapshot final {
    /** @brief Modules sorted by canonical namespace. */
    std::vector<ModuleDescriptor> modules;
    /** @brief Actions sorted by canonical full identifier. */
    std::vector<ActionDescriptor> actions;
    /** @brief Resources sorted by canonical full identifier. */
    std::vector<resource::ResourceDescriptor> resources;
    /** @brief Tasks sorted by canonical identifier. */
    std::vector<task::TaskDescriptor> tasks;
};

} // namespace axiom::introspection
