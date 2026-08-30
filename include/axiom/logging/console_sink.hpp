#pragma once

/**
 * @file console_sink.hpp
 * @brief Colorized stderr sink that formats LogRecord values without exposing spdlog.
 */

#include <axiom/export.hpp>
#include <axiom/logging/log_record.hpp>
#include <axiom/logging/log_sink.hpp>

#include <memory>

namespace axiom::logging {

/**
 * @brief Writes colorized, UTC structured records to standard error.
 *
 * spdlog is intentionally hidden behind this implementation boundary so users of
 * Axiom::Axiom never need to find or link it.
 */
class AXIOM_API ConsoleSink final : public ILogSink {
public:
    /**
     * @brief Creates a sink writing colorized UTC records to standard error.
     * @throws std::bad_alloc If internal sink state cannot be allocated.
     */
    ConsoleSink();
    /** @brief Flushes and destroys the underlying stderr writer. */
    ~ConsoleSink() noexcept override;

    ConsoleSink(const ConsoleSink&) = delete;
    ConsoleSink& operator=(const ConsoleSink&) = delete;

    /**
     * @brief Transfers ownership of the underlying writer from @p other.
     * @param other Sink that becomes empty after the move.
     */
    ConsoleSink(ConsoleSink&&) noexcept;
    /**
     * @brief Replaces this sink's writer with that of @p other.
     * @param other Sink that becomes empty after the move.
     * @return Reference to this sink.
     */
    ConsoleSink& operator=(ConsoleSink&&) noexcept;

    /**
     * @brief Writes one formatted record to standard error.
     * @param record Event to render; nested Value fields are expanded iteratively.
     */
    void consume(const LogRecord& record) override;

    /**
     * @brief Flushes pending stderr output for this sink.
     * @note Safe to call when the sink has been moved-from (no-op).
     */
    void flush() noexcept override;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace axiom::logging
