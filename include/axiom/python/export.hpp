#pragma once

/**
 * @file export.hpp
 * @brief Defines the Axiom Python adapter shared-library import and export annotation.
 */

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef AXIOM_PYTHON_BUILD_SHARED
#define AXIOM_PYTHON_API __declspec(dllexport)
#elif defined(AXIOM_PYTHON_USE_SHARED)
#define AXIOM_PYTHON_API __declspec(dllimport)
#else
#define AXIOM_PYTHON_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#ifdef AXIOM_PYTHON_BUILD_SHARED
#define AXIOM_PYTHON_API __attribute__((visibility("default")))
#else
#define AXIOM_PYTHON_API
#endif
#else
#define AXIOM_PYTHON_API
#endif