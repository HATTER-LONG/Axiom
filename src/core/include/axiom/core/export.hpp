#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(AXIOM_CORE_BUILD)
#define AXIOM_CORE_API __declspec(dllexport)
#else
#define AXIOM_CORE_API __declspec(dllimport)
#endif
#else
#define AXIOM_CORE_API __attribute__((visibility("default")))
#endif
