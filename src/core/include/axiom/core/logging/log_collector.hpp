#pragma once

#include <axiom/core/logging/log_query.hpp>
#include <axiom/core/logging/log_sink.hpp>

#include <cstddef>
#include <mutex>
#include <vector>

namespace axiom::core::logging {

/**
 * @brief Thread-safe, bounded in-memory record collector.
 *
 * The collector retains the most recently consumed records in collection order. A
 * zero capacity accepts records without retaining them.
 */
class LogCollector final : public ILogSink {
public:
    /** @brief Creates a collector retaining up to @p capacity records. */
    explicit LogCollector(std::size_t capacity = 1000);

    /** @brief Retains a copy of @p record, evicting the oldest record when full. */
    void consume(const LogRecord& record) override;

    /** @brief Returns the configured maximum number of retained records. */
    [[nodiscard]] std::size_t capacity() const noexcept;
    /** @brief Returns records matching @p query in ascending collection order. */
    [[nodiscard]] std::vector<LogRecord> records(const LogQuery& query = {}) const;
    /** @brief Alias for records(), provided for query-oriented call sites. */
    [[nodiscard]] std::vector<LogRecord> query(const LogQuery& query) const;

private:
    [[nodiscard]] bool matches(const LogRecord& record, const LogQuery& query) const noexcept;

    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::vector<LogRecord> buffer_;
    std::size_t oldest_{0};
    std::size_t size_{0};
};

} // namespace axiom::core::logging
