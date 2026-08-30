#pragma once

/**
 * @file log_level.hpp
 * @brief Severity levels for structured log records and minimum-level checks.
 */

#include <cstdint>

namespace axiom::logging {

/**
 * @brief Severity assigned to a log record, ordered from least to most severe.
 *
 * Numeric ordering is stable and used by isAtLeast() and sink filters.
 */
enum class LogLevel : std::uint8_t {
    Trace,    ///< Finest diagnostics; typically disabled in production.
    Debug,    ///< Development diagnostics below routine operational messages.
    Info,     ///< Routine operational events.
    Warning,  ///< Unexpected but recoverable conditions.
    Error,    ///< Operation failed; the process may continue.
    Critical, ///< Severe failure that may require process shutdown.
};

/**
 * @brief Returns whether a record at @p level satisfies a minimum severity.
 * @param level Severity of the candidate record.
 * @param minimum Lowest severity that should be accepted.
 * @return true when @p level is at least as severe as @p minimum.
 */
[[nodiscard]] constexpr bool isAtLeast(const LogLevel level, const LogLevel minimum) noexcept {
    return static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(minimum);
}

} // namespace axiom::logging
