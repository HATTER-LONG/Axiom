#pragma once

#include <axiom/core/base/result.hpp>
#include <axiom/core/export.hpp>

#include <functional>
#include <map>
#include <string>

namespace axiom::core {

/** @brief Documents an Action namespace and its caller-facing metadata. */
struct ModuleDescriptor {
    /** @brief Lowercase ASCII namespace used by the module portion of ActionId. */
    std::string namespace_name;
    /** @brief Stable string metadata for discovery adapters. */
    std::map<std::string, std::string, std::less<>> metadata;

    /** @brief Destroys the descriptor without propagating an exception. */
    ~ModuleDescriptor() noexcept = default;
};

/**
 * @brief Validates a Module description before it is registered.
 *
 * @param descriptor Description to inspect without modifying registration state.
 * @return Success when the namespace is valid; otherwise an InvalidDescriptor error.
 */
[[nodiscard]] AXIOM_CORE_API Result<void> validate(const ModuleDescriptor& descriptor);

} // namespace axiom::core
