#pragma once

/**
 * @file descriptor.hpp
 * @brief Action metadata, parameter defaults, and validation.
 */

#include <axiom/action/action_id.hpp>
#include <axiom/export.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace axiom {

/** @brief Documents one named Action input. */
struct ParameterDescriptor {
    /** @brief Lowercase ASCII parameter name. */
    std::string name;
    /** @brief Human-readable explanation of the parameter. */
    std::string description;
    /** @brief Whether callers must supply the parameter. */
    bool required{true};
    /** @brief Shape expected for the parameter's Value. */
    TypeDescriptor type{};
    /** @brief Optional fallback Value for an optional parameter. */
    std::optional<Value> default_value;

    /** @brief Destroys the descriptor without propagating an exception. */
    ~ParameterDescriptor() noexcept = default;
};

/**
 * @brief Documents one callable Axiom capability.
 *
 * Local name and owning Module are taken from @c id; this type does not store a
 * second, possibly divergent name or module string. Optional metadata defaults
 * empty so existing aggregate initialization keeps prior behavior.
 */
struct ActionDescriptor {
    /** @brief Canonical Action identifier. */
    ActionId id;
    /** @brief Human-readable explanation of the Action. */
    std::string description;
    /** @brief Inputs in externally observable presentation and validation order. */
    std::vector<ParameterDescriptor> parameters;
    /** @brief Shape produced by a successful invocation. */
    TypeDescriptor return_type{};
    /** @brief Optional compatible Action version. */
    std::optional<std::string> version;
    /**
     * @brief Searchable labels in registration presentation order.
     *
     * Matching is case-sensitive and exact. Empty or duplicate tags are rejected
     * at validation; they are not silently dropped.
     */
    std::vector<std::string> tags;
    /**
     * @brief Adapter-owned extra string facts keyed in deterministic lexical order.
     *
     * Keys must be non-empty. Empty values are allowed and mean the host supplied
     * an explicit empty string.
     */
    std::map<std::string, std::string, std::less<>> metadata;

    /** @brief Destroys the descriptor without propagating an exception. */
    ~ActionDescriptor() noexcept = default;
};

/**
 * @brief Validates an Action description before it is registered.
 *
 * @param descriptor Description to inspect without modifying registration state.
 * @return Success when all parameter names, nested descriptors, version, tags,
 *         metadata, and defaults are coherent; otherwise an InvalidDescriptor error.
 */
[[nodiscard]] AXIOM_API Result<void> validate(const ActionDescriptor& descriptor);

} // namespace axiom
