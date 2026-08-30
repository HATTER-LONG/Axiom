#pragma once

/**
 * @file log_collector.hpp
 * @brief Thread-safe, bounded in-memory ILogSink for tests and diagnostics.
 */

#include <axiom/core/export.hpp>
#include <axiom/core/logging/log_query.hpp>
#include <axiom/core/logging/log_record.hpp>
#include <axiom/core/logging/log_sink.hpp>

#include <cstddef>
#include <mutex>
#include <vector>

namespace axiom::core::logging {

/**
 * @brief Thread-safe, bounded in-memory record collector.
 *
 * The collector retains the most recently consumed records in collection order. A
 * zero capacity accepts records without retaining them. When the ring buffer is full,
 * the oldest retained record is overwritten.
 */
class AXIOM_CORE_API LogCollector final : public ILogSink {
public:
    /**
     * @brief Creates a collector retaining up to @p capacity records.
     * @param capacity Maximum number of retained records; 0 keeps nothing.
     */
    explicit LogCollector(std::size_t capacity = 1000);

    /**
     * @brief Retains a copy of @p record, evicting the oldest record when full.
     * @param record Event to retain; ignored when capacity is zero.
     */
    void consume(const LogRecord& record) override;

    /**
     * @brief Returns the configured maximum number of retained records.
     * @return Capacity passed to the constructor.
     */
    [[nodiscard]] std::size_t capacity() const noexcept;

    /**
     * @brief Returns records matching @p query in ascending collection order.
     * @param query Severity, category-prefix, and optional newest-N limit.
     * @return Matching records; when @p query.limit is non-zero, only the newest
     *         matches are kept, still ordered oldest-to-newest within that subset.
     */
    [[nodiscard]] std::vector<LogRecord> records(const LogQuery& query = {}) const;

    /**
     * @brief Alias for records(), provided for query-oriented call sites.
     * @param query Severity, category-prefix, and optional newest-N limit.
     * @return Same result as records(@p query).
     */
    [[nodiscard]] std::vector<LogRecord> query(const LogQuery& query) const;

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::vector<LogRecord> buffer_;
    std::size_t oldest_{0};
    std::size_t size_{0};
};

} // namespace axiom::core::logging
