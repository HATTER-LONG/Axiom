#include <axiom/logging/log_collector.hpp>

#include <axiom/logging/log_level.hpp>
#include <axiom/logging/log_query.hpp>
#include <axiom/logging/log_record.hpp>

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace axiom::logging {

namespace {

// Same segment-boundary rule as LogFilter::matches (prefix "runtime" ≠ "runtime2").
[[nodiscard]] bool matchesPrefix(const std::string_view category,
                                 const std::string_view prefix) noexcept {
    return prefix.empty() || category == prefix ||
           (category.starts_with(prefix) && category[prefix.size()] == '.');
}

[[nodiscard]] bool matchesExactField(const LogRecord& record,
                                     const std::string_view key,
                                     const std::optional<std::string>& expected) {
    if(!expected.has_value()) {
        return true;
    }
    const auto found = record.fields.find(key);
    return found != record.fields.end() && found->second.isString() &&
           found->second.asString() == *expected;
}

[[nodiscard]] bool matches(const LogRecord& record, const LogQuery& log_query) {
    return isAtLeast(record.level, log_query.minimum_level) &&
           (log_query.category_prefixes.empty() ||
            std::ranges::any_of(log_query.category_prefixes,
                                [&record](const std::string& prefix) {
                                    return matchesPrefix(record.category, prefix);
                                })) &&
           matchesExactField(record, "request_id", log_query.request_id) &&
           matchesExactField(record, "trace_id", log_query.trace_id) &&
           matchesExactField(record, "action", log_query.action_id) &&
           matchesExactField(record, "task_id", log_query.task_id);
}

} // namespace

LogCollector::LogCollector(const std::size_t capacity) : capacity_(capacity) {
    buffer_.reserve(capacity);
}

void LogCollector::consume(const LogRecord& record) {
    const std::scoped_lock lock{mutex_};
    if(capacity_ == 0) {
        // Zero capacity is a deliberate drop-all sink, not an error.
        return;
    }
    if(size_ < capacity_) {
        buffer_.push_back(record);
        ++size_;
        return;
    }
    // Ring buffer: overwrite the oldest slot and advance the read cursor.
    buffer_.at(oldest_) = record;
    oldest_ = (oldest_ + 1) % capacity_;
}

std::size_t LogCollector::capacity() const noexcept { return capacity_; }

std::vector<LogRecord> LogCollector::records(const LogQuery& log_query) const {
    std::vector<LogRecord> result;
    const std::scoped_lock lock{mutex_};
    result.reserve(size_);
    for(std::size_t offset = 0; offset < size_; ++offset) {
        const auto index = (oldest_ + offset) % capacity_;
        const auto& record = buffer_.at(index);
        if(matches(record, log_query)) {
            result.push_back(record);
        }
    }
    if(log_query.limit != 0) {
        // Keep the newest N matches; erase from the front so remaining order stays
        // oldest-to-newest within that subset.
        const auto first = result.size() - std::min(result.size(), log_query.limit);
        result.erase(result.begin(), result.begin() + static_cast<std::ptrdiff_t>(first));
    }
    return result;
}

std::vector<LogRecord> LogCollector::query(const LogQuery& log_query) const {
    return records(log_query);
}

} // namespace axiom::logging
