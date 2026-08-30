#pragma once

/**
 * @file resource_serial.hpp
 * @brief Process-wide resource serial allocation and a non-installed overflow test seam.
 *
 * This header is not part of the installed Core public surface. Tests may include it
 * through a non-install include path. Serial exhaustion is not a switch on
 * ResourceRegistry.
 */

#include <axiom/export.hpp>
#include <axiom/foundation/result.hpp>

#include <cstdint>

namespace axiom::resource::detail {

/**
 * @brief Allocates the next process-wide resource serial.
 *
 * Serials start at 1, are never reused, and are unique across Registries in this
 * process. Exhaustion returns InternalError and does not wrap around to a usable
 * serial.
 *
 * @return A non-zero serial, or InternalError when the serial space is exhausted.
 */
[[nodiscard]] Result<std::uint64_t> allocateResourceSerial();

/**
 * @brief Forces serial exhaustion for the lifetime of the guard, then restores it.
 *
 * Intended only for tests of the overflow failure path.
 */
class AXIOM_API ResourceSerialExhaustionGuard {
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

} // namespace axiom::resource::detail
