#pragma once

/**
 * @file resource_descriptor.hpp
 * @brief Read-only value description of a registered resource.
 */

#include <axiom/resource/resource_id.hpp>

#include <string>

namespace axiom::resource {

/**
 * @brief Describes the identity and logical type of one registry member.
 *
 * This is an owning value snapshot. It contains no resource object, keepalive,
 * registry association, or C++ RTTI. The descriptor remains valid independently
 * of subsequent registry changes.
 */
struct ResourceDescriptor final {
    /** @brief Stable identity of the registered resource. */
    ResourceId id;
    /** @brief Logical type name supplied by the resource's ResourceTraits. */
    std::string type;
};

} // namespace axiom::resource
