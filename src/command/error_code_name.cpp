#include <axiom/command/error_code_name.hpp>

#include <axiom/foundation/error.hpp>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace axiom::command {
namespace {

constexpr std::array error_code_names{
    "invalid_argument", "missing_argument", "unknown_argument",   "type_mismatch",
    "not_found",        "already_exists",   "invalid_descriptor", "invocation_failed",
    "internal_error",   "cancelled",        "unknown_command",    "host_closed",
};
static_assert(error_code_names.size() == static_cast<std::size_t>(ErrorCode::HostClosed) + 1);

} // namespace

std::string_view errorCodeName(const ErrorCode code) {
    const auto index = static_cast<std::size_t>(code);
    if(index >= error_code_names.size()) {
        throw std::logic_error{"Unknown ErrorCode enumerator"};
    }
    return error_code_names[index];
}

} // namespace axiom::command