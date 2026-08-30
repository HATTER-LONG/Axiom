#include "resource_serial.hpp"

#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>

#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>

namespace axiom::resource::detail {
namespace {

std::atomic<std::uint64_t>& serialCursor() noexcept {
    static std::atomic<std::uint64_t> next{1};
    return next;
}

[[nodiscard]] Error serialExhaustedError() {
    return {
        .code = ErrorCode::InternalError,
        .message = "Resource identity serial space is exhausted",
        .path = std::nullopt,
        .details = std::nullopt,
    };
}

} // namespace

Result<std::uint64_t> allocateResourceSerial() {
    auto& cursor = serialCursor();
    std::uint64_t current = cursor.load(std::memory_order_relaxed);
    while(current != 0U) {
        const std::uint64_t successor =
            current == std::numeric_limits<std::uint64_t>::max() ? 0U : current + 1U;
        if(cursor.compare_exchange_weak(current, successor, std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
            return Result<std::uint64_t>::success(current);
        }
    }
    return Result<std::uint64_t>::failure(serialExhaustedError());
}

ResourceSerialExhaustionGuard::ResourceSerialExhaustionGuard()
    : previous_(serialCursor().exchange(0U, std::memory_order_relaxed)) {}

ResourceSerialExhaustionGuard::~ResourceSerialExhaustionGuard() {
    serialCursor().store(previous_, std::memory_order_relaxed);
}

} // namespace axiom::resource::detail
