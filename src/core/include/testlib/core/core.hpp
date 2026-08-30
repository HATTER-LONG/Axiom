#pragma once

/**
 * @file core.hpp
 * @brief Public Core entry point for the scaffolded library.
 */

#include <testlib/core/export.hpp>

namespace testlib::core {

/**
 * @brief Returns the library ABI version encoded by this build.
 */
[[nodiscard]] TESTLIB_CORE_API int version();

} // namespace testlib::core
