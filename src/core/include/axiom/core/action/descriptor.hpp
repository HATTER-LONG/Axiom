#pragma once

#include <axiom/core/action/action_id.hpp>
#include <axiom/core/base/type_descriptor.hpp>

#include <optional>
#include <string>
#include <vector>

namespace axiom::core {

/** @brief Documents one named Action input. */
struct ParameterDescriptor {
    /** @brief Lowercase ASCII parameter name. */
    std::string name;
    /** @brief Human-readable explanation of the parameter. */
    std::string description;
    /** @brief Whether callers must supply the parameter. */
    bool required{true};
    /** @brief Shape expected for the parameter's Value. */
    TypeDescriptor type;
    /** @brief Optional fallback Value for an optional parameter. */
    std::optional<Value> default_value;

    /** @brief Destroys the descriptor without propagating an exception. */
    ~ParameterDescriptor() noexcept = default;
};

/** @brief Documents one callable Core capability. */
struct ActionDescriptor {
    /** @brief Canonical Action identifier. */
    ActionId id;
    /** @brief Human-readable explanation of the Action. */
    std::string description;
    /** @brief Inputs in externally observable presentation and validation order. */
    std::vector<ParameterDescriptor> parameters;
    /** @brief Shape produced by a successful invocation. */
    TypeDescriptor return_type;
    /** @brief Optional compatible Action version. */
    std::optional<std::string> version;
    /** @brief Searchable labels associated with this Action. */
    std::vector<std::string> tags;

    /** @brief Destroys the descriptor without propagating an exception. */
    ~ActionDescriptor() noexcept = default;
};

/**
 * @brief Validates an Action description before it is registered.
 *
 * @param descriptor Description to inspect without modifying registration state.
 * @return Success when all parameter names, nested descriptors, version, and defaults
 *         are coherent; otherwise an InvalidDescriptor error.
 */
[[nodiscard]] Result<void> validate(const ActionDescriptor& descriptor);

} // namespace axiom::core
