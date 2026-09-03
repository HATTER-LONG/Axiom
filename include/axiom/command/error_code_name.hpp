#pragma once

/**
 * @file error_code_name.hpp
 * @brief Stable lowercase protocol name for every ErrorCode.
 */

#include <axiom/export.hpp>
#include <axiom/foundation/error.hpp>

#include <string_view>

namespace axiom::command {

/**
 * @brief Returns the stable lowercase protocol name of an ErrorCode.
 *
 * This is the single authority for the dynamic protocol spelling used by
 * Command encodings and language adapters, for example `type_mismatch`.
 *
 * @param code ErrorCode to name.
 * @return Stable view of the lowercase protocol name.
 * @throws std::logic_error If code is not a valid enumerator.
 */
[[nodiscard]] AXIOM_API std::string_view errorCodeName(ErrorCode code);

} // namespace axiom::command