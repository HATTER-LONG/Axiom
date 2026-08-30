#pragma once

/**
 * @file resource_serial.hpp
 * @brief Process-wide resource serial allocation and a private overflow test seam.
 */

#include <axiom/core/base/result.hpp>
#include <axiom/core/export.hpp>

#include <cstdint>

namespace axiom::core::resource::detail {

/**
 * @brief Allocates the next process-wide resource serial.
 *
 * Serials start at 1, are never reused, and are unique across Registries in this
 * process. Exhaustion returns InternalError and does not wrap around to a usable
 * serial.
 *
 * @return A non-zero serial, or InternalError when the serial space is exhausted.
 * @note This function is not a stable public API.
 */
[[nodiscard]] AXIOM_CORE_API Result<std::uint64_t> allocateResourceSerial();

/**
 * @brief Forces serial exhaustion for the lifetime of the guard, then restores it.
 *
 * Intended only for tests of the overflow failure path. It is not a stable public
 * API and is not a switch on ResourceRegistry.
 */
class AXIOM_CORE_API ResourceSerialExhaustionGuard {
public:
    ResourceSerialExhaustionGuard();
    ~ResourceSerialExhaustionGuard();

    ResourceSerialExhaustionGuard(const ResourceSerialExhaustionGuard&) = delete;
    ResourceSerialExhaustionGuard(ResourceSerialExhaustionGuard&&) = delete;
    ResourceSerialExhaustionGuard& operator=(const ResourceSerialExhaustionGuard&) = delete;
    ResourceSerialExhaustionGuard& operator=(ResourceSerialExhaustionGuard&&) = delete;

private:
    std::uint64_t previous_{0};
};

} // namespace axiom::core::resource::detail
