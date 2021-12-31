#pragma once

// Based on https://sourceforge.net/p/predef/wiki/Home/

#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS
#elif defined(__ANDROID__)
#define PLATFORM_ANDROID
#elif defined(__linux__)
#define PLATFORM_LINUX
#elif defined(__APPLE__)
#define PLATFORM_MACOS
#elif defined(__asmjs__)
#define PLATFORM_ASMJS
#else
#error "Undefined platform"
#endif

#if defined(_MSC_VER)
#define COMPILER_MSVC
#elif defined(__MINGW32__) || defined(__MINGW64__)
#define COMPILER_MINGW
#elif defined(__clang__)
#define COMPILER_CLANG
#elif defined(__GNUC__)
#define COMPILER_GCC
#elif defined(__EMSCRIPTEN__)
#define COMPILER_EMSCRIPTEN
#else
#error "Undefined compiler"
#endif

#if defined(__i386__) || defined(_M_IX86)
#define ARCHITECTURE_I386
#elif defined(__amd64__) || defined(__x86_64__) || defined(_M_AMD64)
#define ARCHITECTURE_AMD64
#elif defined(__arm__) || defined(_M_ARM)
#define ARCHITECTURE_ARM
#else
#error "Undefined architecture"
#endif

#if __STDC_VERSION__ == 201112L
#define STANDARD_C11
#elif __STDC_VERSION__ == 199901L
#define STANDARD_C99
#elif __STDC_VERSION__ == 199409L
#define STANDARD_C94
#elif defined(__STDC__)
#define STANDARD_C89
#else
#if defined(COMPILER_MSVC)
#define STANDARD_C89
#else
#error "Undefined C standard"
#endif
#endif

#if __cplusplus == 201703L
#define STANDARD_CXX17
#elif __cplusplus == 201402L
#define STANDARD_CXX14
#elif __cplusplus == 201103L
#define STANDARD_CXX11
#elif __cplusplus == 199711L
#define STANDARD_CXX98
#elif defined(__cplusplus)
#error "Undefined C++ standard"
#endif

#if (defined(STANDARD_C11) || defined(STANDARD_C99)) && !defined(__STDC_NO_ATOMICS__)
#define STDC_HAS_ATOMICS
#endif
#if (defined(STANDARD_C11) || defined(STANDARD_C99)) && !defined(__STDC_NO_THREADS__)
#define STDC_HAS_THREADS
#endif

#if defined(_DEBUG) || defined(DEBUG)
#define CONFIGURATION_DEBUG
#else
#define CONFIGURATION_RELEASE
#endif

#if defined(COMPILER_MSVC)
#if !defined(__cplusplus)
#define thread_local __declspec(thread)
#endif /*__cplusplus*/
#else  /*!COMPILER_MSVC*/
#include <threads.h>
#endif /*COMPILER_MSVC*/
