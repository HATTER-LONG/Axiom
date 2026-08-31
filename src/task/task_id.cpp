#include <axiom/task/task_id.hpp>

#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace axiom::task {
namespace {
[[nodiscard]] Error invalidTaskId() {
    return {.code = ErrorCode::InvalidArgument,
            .message = "Task ID must have the form task:<non-zero decimal serial>",
            .path = std::nullopt,
            .details = std::nullopt};
}
} // namespace

Result<TaskId> TaskId::parse(const std::string_view text) {
    constexpr std::string_view prefix{"task:"};
    if(!text.starts_with(prefix)) {
        return Result<TaskId>::failure(invalidTaskId());
    }
    const auto serial_text = text.substr(prefix.size());
    if(serial_text.empty() || (serial_text.size() > 1U && serial_text.front() == '0')) {
        return Result<TaskId>::failure(invalidTaskId());
    }
    std::uint64_t serial = 0;
    const auto parsed =
        std::from_chars(serial_text.data(), serial_text.data() + serial_text.size(), serial);
    if(parsed.ec != std::errc{} || parsed.ptr != serial_text.data() + serial_text.size() ||
       serial == 0U) {
        return Result<TaskId>::failure(invalidTaskId());
    }
    return Result<TaskId>::success(TaskId{std::string{text}});
}
} // namespace axiom::task
