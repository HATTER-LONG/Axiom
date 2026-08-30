#include <axiom/resource/resource_id.hpp>

#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/resource/resource_traits.hpp>

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace axiom::resource {
namespace {

[[nodiscard]] Error invalidResourceId() {
    return {
        .code = ErrorCode::InvalidArgument,
        .message = "Resource ID must have the form <type>:<serial> with a canonical type name and "
                   "a non-zero decimal serial",
        .path = std::nullopt,
        .details = std::nullopt,
    };
}

[[nodiscard]] bool isCanonicalSerial(const std::string_view text) noexcept {
    if(text.empty() || (text.size() > 1U && text.front() == '0')) {
        return false;
    }
    std::uint64_t serial = 0;
    const char* const begin = text.data();
    const auto parsed = std::from_chars(begin, begin + text.size(), serial);
    return parsed.ec == std::errc{} && parsed.ptr == begin + text.size() && serial != 0U;
}

} // namespace

Result<ResourceId> ResourceId::parse(const std::string_view text) {
    const auto separator = text.find(':');
    if(separator == std::string_view::npos || separator != text.rfind(':') || separator == 0U ||
       separator + 1U == text.size() || !detail::isCanonicalTypeName(text.substr(0U, separator)) ||
       !isCanonicalSerial(text.substr(separator + 1U))) {
        return Result<ResourceId>::failure(invalidResourceId());
    }
    return Result<ResourceId>::success(ResourceId{std::string{text}});
}

} // namespace axiom::resource
