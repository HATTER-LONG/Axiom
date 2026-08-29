#include <axiom/core/logging/log_collector.hpp>

#include <algorithm>
#include <string_view>

namespace axiom::core::logging {

namespace {

[[nodiscard]] bool matchesPrefix(const std::string_view category,
                                 const std::string_view prefix) noexcept {
    return prefix.empty() || category == prefix ||
           (category.starts_with(prefix) && category[prefix.size()] == '.');
}

} // namespace

LogCollector::LogCollector(const std::size_t capacity) : capacity_(capacity) {
    buffer_.reserve(capacity);
}

void LogCollector::consume(const LogRecord& record) {
    std::lock_guard lock{mutex_};
    if(capacity_ == 0) {
        return;
    }
    if(size_ < capacity_) {
        buffer_.push_back(record);
        ++size_;
        return;
    }
    buffer_.at(oldest_) = record;
    oldest_ = (oldest_ + 1) % capacity_;
}

std::size_t LogCollector::capacity() const noexcept { return capacity_; }

std::vector<LogRecord> LogCollector::records(const LogQuery& query) const {
    std::vector<LogRecord> result;
    std::lock_guard lock{mutex_};
    result.reserve(size_);
    for(std::size_t offset = 0; offset < size_; ++offset) {
        const auto index = (oldest_ + offset) % capacity_;
        const auto& record = buffer_.at(index);
        if(matches(record, query)) {
            result.push_back(record);
        }
    }
    if(query.limit != 0) {
        const auto first = result.size() - std::min(result.size(), query.limit);
        result.erase(result.begin(), result.begin() + static_cast<std::ptrdiff_t>(first));
    }
    return result;
}

std::vector<LogRecord> LogCollector::query(const LogQuery& query) const { return records(query); }

bool LogCollector::matches(const LogRecord& record, const LogQuery& query) const noexcept {
    return isAtLeast(record.level, query.minimum_level) &&
           (query.category_prefixes.empty() ||
            std::ranges::any_of(query.category_prefixes, [&record](const std::string& prefix) {
                return matchesPrefix(record.category, prefix);
            }));
}

} // namespace axiom::core::logging
