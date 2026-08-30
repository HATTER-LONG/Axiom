#pragma once

/**
 * @file export.hpp
 * @brief Defines the Core shared-library import and export annotation.
 *
 * CMake defines TESTLIB_CORE_BUILD_SHARED while producing a shared
 * Testlib::Core library and TESTLIB_CORE_USE_SHARED for its consumers.
 * Static builds leave the annotation empty. The macro maps to the platform's native visibility
 * mechanism on Windows, Linux, and macOS.
 */

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef TESTLIB_CORE_BUILD_SHARED
#define TESTLIB_CORE_API __declspec(dllexport)
#elif defined(TESTLIB_CORE_USE_SHARED)
#define TESTLIB_CORE_API __declspec(dllimport)
#else
#define TESTLIB_CORE_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#ifdef TESTLIB_CORE_BUILD_SHARED
#define TESTLIB_CORE_API __attribute__((visibility("default")))
#else
#define TESTLIB_CORE_API
#endif
#else
#define TESTLIB_CORE_API
#endif
