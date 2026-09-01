#pragma once

/**
 * @file module.hpp
 * @brief Module namespace metadata and validation.
 */

#include <axiom/export.hpp>
#include <axiom/foundation/result.hpp>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace axiom {

/**
 * @brief Documents an Action namespace and its caller-facing discovery fields.
 *
 * @c namespace_name is the canonical Module identity. Do not add a second identifier
 * that repeats that namespace. Optional version, tags, and metadata default so
 * existing aggregate initialization keeps prior behavior.
 */
struct ModuleDescriptor {
    /** @brief Lowercase ASCII namespace used by the module portion of ActionId. */
    std::string namespace_name;
    /** @brief Human-readable explanation of the Module. */
    std::string description{};
    /** @brief Optional compatible Module version; omitted means unspecified. */
    std::optional<std::string> version{};
    /**
     * @brief Searchable labels in registration presentation order.
     *
     * Matching is case-sensitive and exact. Empty or duplicate tags are rejected
     * at validation; they are not silently dropped.
     */
    std::vector<std::string> tags{};
    /**
     * @brief Adapter-owned extra string facts keyed in deterministic lexical order.
     *
     * Keys must be non-empty. Empty values are allowed and mean the host supplied
     * an explicit empty string.
     */
    std::map<std::string, std::string, std::less<>> metadata{};

    /** @brief Destroys the descriptor without propagating an exception. */
    ~ModuleDescriptor() noexcept = default;
};

/**
 * @brief Validates a Module description before it is registered.
 *
 * @param descriptor Description to inspect without modifying registration state.
 * @return Success when the namespace, optional version, tags, and metadata are
 *         coherent; otherwise an InvalidDescriptor error.
 */
[[nodiscard]] AXIOM_API Result<void> validate(const ModuleDescriptor& descriptor);

} // namespace axiom
