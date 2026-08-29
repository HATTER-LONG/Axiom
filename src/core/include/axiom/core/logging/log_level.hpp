#pragma once

#include <cstdint>

namespace axiom::core::logging {

/** @brief Severity assigned to a log record, ordered from least to most severe. */
enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warning, Error, Critical };

/** @brief Returns whether a record at @p level satisfies a minimum severity. */
[[nodiscard]] constexpr bool isAtLeast(const LogLevel level, const LogLevel minimum) noexcept {
    return static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(minimum);
}

} // namespace axiom::core::logging
