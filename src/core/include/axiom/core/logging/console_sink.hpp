#pragma once

#include <axiom/core/logging/log_record.hpp>
#include <axiom/core/logging/log_sink.hpp>

#include <memory>

namespace axiom::core::logging {

/**
 * @brief Writes colorized, UTC structured records to standard error.
 *
 * spdlog is intentionally hidden behind this implementation boundary so users of
 * Axiom::Core never need to find or link it.
 */
class ConsoleSink final : public ILogSink {
public:
    ConsoleSink();
    ~ConsoleSink() noexcept override;

    ConsoleSink(const ConsoleSink&) = delete;
    ConsoleSink& operator=(const ConsoleSink&) = delete;
    ConsoleSink(ConsoleSink&&) noexcept;
    ConsoleSink& operator=(ConsoleSink&&) noexcept;

    /** @brief Writes one formatted record to standard error. */
    void consume(const LogRecord& record) override;
    void flush() noexcept override;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace axiom::core::logging
