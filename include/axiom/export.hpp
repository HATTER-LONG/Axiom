#pragma once

/**
 * @file export.hpp
 * @brief Defines the Axiom shared-library import and export annotation.
 *
 * CMake defines AXIOM_BUILD_SHARED while producing a shared Axiom::Axiom
 * library and AXIOM_USE_SHARED for its consumers. Static builds leave the
 * annotation empty. The macro maps to the platform's native visibility mechanism
 * on Windows, Linux, and macOS.
 */

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef AXIOM_BUILD_SHARED
#define AXIOM_API __declspec(dllexport)
#elif defined(AXIOM_USE_SHARED)
#define AXIOM_API __declspec(dllimport)
#else
#define AXIOM_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#ifdef AXIOM_BUILD_SHARED
#define AXIOM_API __attribute__((visibility("default")))
#else
#define AXIOM_API
#endif
#else
#define AXIOM_API
#endif
