#pragma once

#include "./platform.h"

#include <stdbool.h>

#if defined(PLATFORM_WINDOWS)

#include <intrin.h>
#if defined(ARCHITECTURE_I386) || defined(ARCHITECTURE_AMD64)
#define custom_atomic_cpu_relax() _mm_pause()
#else
#define custom_atomic_cpu_relax() YieldProcessor()
#endif

#elif defined(COMPILER_CLANG) || defined(COMPILER_GCC) || defined(COMPILER_MINGW)

#if defined(ARCHITECTURE_I386) || defined(ARCHITECTURE_AMD64)
#define custom_atomic_cpu_relax() __asm__ __volatile__("pause")
#elif defined(ARCHITECTURE_ARM)
#define custom_atomic_cpu_relax() __asm__ __volatile__("yield")
#endif

#endif

#if !defined(custom_atomic_cpu_relax)
#error "Cannot define custom_atomic_cpu_relax"
#endif

#if defined(STDC_HAS_ATOMICS)

#include <stdatomic.h>

#define custom_atomic_flag atomic_flag

#define custom_atomic_bool atomic_bool
#define custom_atomic_char atomic_char
#define custom_atomic_schar atomic_schar
#define custom_atomic_uchar atomic_uchar
#define custom_atomic_short atomic_short
#define custom_atomic_ushort atomic_ushort
#define custom_atomic_int atomic_int
#define custom_atomic_uint atomic_uint
#define custom_atomic_long atomic_long
#define custom_atomic_ulong atomic_ulong
#define custom_atomic_llong atomic_llong
#define custom_atomic_ullong atomic_ullong
#define custom_atomic_char16_t atomic_char16_t
#define custom_atomic_char32_t atomic_char32_t
#define custom_atomic_wchar_t atomic_wchar_t
#define custom_atomic_intptr_t atomic_intptr_t
#define custom_atomic_uintptr_t atomic_uintptr_t
#define custom_atomic_size_t atomic_size_t
#define custom_atomic_ptrdiff_t atomic_ptrdiff_t
#define custom_atomic_intmax_t atomic_intmax_t
#define custom_atomic_uintmax_t atomic_uintmax_t

#define custom_atomic_uint8_t custom_atomic_uchar
#define custom_atomic_uint16_t custom_atomic_ushort
#define custom_atomic_uint32_t custom_atomic_ulong
#define custom_atomic_uint64_t custom_atomic_ullong
#define custom_atomic_int8_t custom_atomic_schar
#define custom_atomic_int16_t custom_atomic_short
#define custom_atomic_int32_t custom_atomic_long
#define custom_atomic_int64_t custom_atomic_llong

#define CUSTOM_ATOMIC_FLAG_INIT ATOMIC_FLAG_INIT

#define custom_atomic_flag_test_and_set atomic_flag_test_and_set
#define custom_atomic_flag_clear atomic_flag_clear

#define CUSTOM_ATOMIC_VAR_INIT ATOMIC_VAR_INIT

#define custom_atomic_init atomic_init
#define custom_atomic_store atomic_store
#define custom_atomic_load atomic_load

#define custom_atomic_exchange atomic_exchange
#define custom_atomic_compare_exchange_strong atomic_compare_exchange_strong
#define custom_atomic_compare_exchange_weak atomic_compare_exchange_weak

#define custom_atomic_fetch_add atomic_fetch_add
#define custom_atomic_fetch_sub atomic_fetch_sub
#define custom_atomic_fetch_or atomic_fetch_or
#define custom_atomic_fetch_xor atomic_fetch_xor
#define custom_atomic_fetch_and atomic_fetch_and

#define HAS_ATOMIC_64

#elif defined(STANDARD_CXX11) || defined(STANDARD_CXX14) || defined(STANDARD_CXX17)

#include <atomic>

#define custom_atomic_flag std::atomic_flag

#define custom_atomic_bool std::atomic_bool
#define custom_atomic_char std::atomic_char
#define custom_atomic_schar std::atomic_schar
#define custom_atomic_uchar std::atomic_uchar
#define custom_atomic_short std::atomic_short
#define custom_atomic_ushort std::atomic_ushort
#define custom_atomic_int std::atomic_int
#define custom_atomic_uint std::atomic_uint
#define custom_atomic_long std::atomic_long
#define custom_atomic_ulong std::atomic_ulong
#define custom_atomic_llong std::atomic_llong
#define custom_atomic_ullong std::atomic_ullong
#define custom_atomic_char16_t std::atomic_char16_t
#define custom_atomic_char32_t std::atomic_char32_t
#define custom_atomic_wchar_t std::atomic_wchar_t
#define custom_atomic_intptr_t std::atomic_intptr_t
#define custom_atomic_uintptr_t std::atomic_uintptr_t
#define custom_atomic_size_t std::atomic_size_t
#define custom_atomic_ptrdiff_t std::atomic_ptrdiff_t
#define custom_atomic_intmax_t std::atomic_intmax_t
#define custom_atomic_uintmax_t std::atomic_uintmax_t

#define custom_atomic_uint8_t std::atomic_uint8_t
#define custom_atomic_uint16_t std::atomic_uint16_t
#define custom_atomic_uint32_t std::atomic_uint32_t
#define custom_atomic_uint64_t std::atomic_uint64_t
#define custom_atomic_int8_t std::atomic_int8_t
#define custom_atomic_int16_t std::atomic_int16_t
#define custom_atomic_int32_t std::atomic_int32_t
#define custom_atomic_int64_t std::atomic_int64_t

#define CUSTOM_ATOMIC_FLAG_INIT ATOMIC_FLAG_INIT

#define custom_atomic_flag_test_and_set std::atomic_flag_test_and_set
#define custom_atomic_flag_clear std::atomic_flag_clear

#define CUSTOM_ATOMIC_VAR_INIT ATOMIC_VAR_INIT

#define custom_atomic_init std::atomic_init
#define custom_atomic_store std::atomic_store
#define custom_atomic_load std::atomic_load

#define custom_atomic_exchange std::atomic_exchange
#define custom_atomic_compare_exchange_strong std::atomic_compare_exchange_strong
#define custom_atomic_compare_exchange_weak std::atomic_compare_exchange_weak

#define custom_atomic_fetch_add std::atomic_fetch_add
#define custom_atomic_fetch_sub std::atomic_fetch_sub
#define custom_atomic_fetch_or std::atomic_fetch_or
#define custom_atomic_fetch_xor std::atomic_fetch_xor
#define custom_atomic_fetch_and std::atomic_fetch_and

#define HAS_ATOMIC_64

#else

#include <intrin.h>
#include <stdbool.h>
#include <stdint.h>
#include <uchar.h>

#if !defined(ARCHITECTURE_I386)
#define HAS_ATOMIC_64
#endif

typedef struct custom_atomic_flag {
  volatile uint8_t value;
} custom_atomic_flag;

typedef volatile bool custom_atomic_bool;
typedef volatile char custom_atomic_char;
typedef volatile signed char custom_atomic_schar;
typedef volatile unsigned char custom_atomic_uchar;
typedef volatile short custom_atomic_short;
typedef volatile unsigned short custom_atomic_ushort;
typedef volatile int custom_atomic_int;
typedef volatile unsigned custom_atomic_uint;
typedef volatile long custom_atomic_long;
typedef volatile unsigned long custom_atomic_ulong;
typedef volatile long long custom_atomic_llong;
typedef volatile unsigned long long custom_atomic_ullong;
typedef volatile char16_t custom_atomic_char16_t;
typedef volatile char32_t custom_atomic_char32_t;
typedef volatile wchar_t custom_atomic_wchar_t;
typedef volatile intptr_t custom_atomic_intptr_t;
typedef volatile uintptr_t custom_atomic_uintptr_t;
typedef volatile size_t custom_atomic_size_t;
typedef volatile ptrdiff_t custom_atomic_ptrdiff_t;
typedef volatile intmax_t custom_atomic_intmax_t;
typedef volatile uintmax_t custom_atomic_uintmax_t;

#define custom_atomic_uint8_t custom_atomic_uchar
#define custom_atomic_uint16_t custom_atomic_ushort
#define custom_atomic_uint32_t custom_atomic_ulong
#define custom_atomic_int8_t custom_atomic_schar
#define custom_atomic_int16_t custom_atomic_short
#define custom_atomic_int32_t custom_atomic_long

#if defined(HAS_ATOMIC_64)
#define custom_atomic_uint64_t custom_atomic_ullong
#define custom_atomic_int64_t custom_atomic_llong
#endif

#define CUSTOM_ATOMIC_FLAG_INIT                                                                                                \
  { 0 }

#define custom_atomic_flag_test_and_set(_obj) (false != _InterlockedExchange8((volatile char *)&(_obj)->value, true))
#define custom_atomic_flag_clear(_obj) _InterlockedExchange8((volatile char *)&(_obj)->value, (uint8_t) false)

#define CUSTOM_ATOMIC_VAR_INIT(_value) (_value)

#define custom_atomic_init custom_atomic_exchange

#define custom_atomic_store custom_atomic_exchange

#define custom_atomic_load(_obj) (*(_obj))

#if defined(HAS_ATOMIC_64)

#define custom_atomic_exchange(_obj, _desired)                                                                                 \
  ((1 == sizeof(*(_obj))                                                                                                       \
        ? (uint8_t)_InterlockedExchange8((volatile char *)(_obj), (uint8_t)(_desired))                                         \
        : (2 == sizeof(*(_obj))                                                                                                \
               ? (uint16_t)_InterlockedExchange16((volatile short *)(_obj), (short)(_desired))                                 \
               : (4 == sizeof(*(_obj)) ? (uint32_t)_InterlockedExchange((volatile long *)(_obj), (long)(_desired))             \
                                       : (uint64_t)_InterlockedExchange64((volatile __int64 *)(_obj), (__int64)(_desired))))))

#define custom_atomic_compare_exchange_strong(_obj, _expected, _desired)                                                       \
  ((1 == sizeof(*(_obj))                                                                                                       \
        ? custom_atomic_implementation_compare_exchange_uint8_t((volatile uint8_t *)(_obj), (uint8_t *)(_expected),            \
                                                                (uint8_t)(_desired))                                           \
        : (2 == sizeof(*(_obj))                                                                                                \
               ? custom_atomic_implementation_compare_exchange_uint16_t((volatile uint16_t *)(_obj), (uint16_t *)(_expected),  \
                                                                        (uint16_t)(_desired))                                  \
               : (4 == sizeof(*(_obj)) ? custom_atomic_implementation_compare_exchange_uint32_t(                               \
                                             (volatile uint32_t *)(_obj), (uint32_t *)(_expected), (uint32_t)(_desired))       \
                                       : custom_atomic_implementation_compare_exchange_uint64_t(                               \
                                             (volatile uint64_t *)(_obj), (uint64_t *)(_expected), (uint64_t)(_desired))))))
#define custom_atomic_compare_exchange_weak custom_atomic_compare_exchange_strong

#define custom_atomic_fetch_add(_obj, _arg)                                                                                    \
  ((1 == sizeof(*(_obj))                                                                                                       \
        ? (uint8_t)_InterlockedExchangeAdd8((volatile char *)(_obj), (char)(_arg))                                             \
        : (2 == sizeof(*(_obj))                                                                                                \
               ? (uint16_t)_InterlockedExchangeAdd16((volatile short *)(_obj), (short)(_arg))                                  \
               : (4 == sizeof(*(_obj)) ? (uint32_t)_InterlockedExchangeAdd((volatile long *)(_obj), (long)(_arg))              \
                                       : (uint64_t)_InterlockedExchangeAdd64((volatile __int64 *)(_obj), (__int64)(_arg))))))

#define custom_atomic_fetch_sub(_obj, _arg)                                                                                    \
  ((1 == sizeof(*(_obj))                                                                                                       \
        ? (uint8_t)_InterlockedExchangeAdd8((volatile char *)(_obj), -(char)(_arg))                                            \
        : (2 == sizeof(*(_obj))                                                                                                \
               ? (uint16_t)_InterlockedExchangeAdd16((volatile short *)(_obj), -(short)(_arg))                                 \
               : (4 == sizeof(*(_obj)) ? (uint32_t)_InterlockedExchangeAdd((volatile long *)(_obj), -(long)(_arg))             \
                                       : (uint64_t)_InterlockedExchangeAdd64((volatile __int64 *)(_obj), -(__int64)(_arg))))))

#define custom_atomic_fetch_and(_obj, _arg)                                                                                    \
  ((1 == sizeof(*(_obj))                                                                                                       \
        ? (uint8_t)_InterlockedAnd8((volatile char *)(_obj), (char)(_arg))                                                     \
        : (2 == sizeof(*(_obj))                                                                                                \
               ? (uint16_t)_InterlockedAnd16((volatile short *)(_obj), (short)(_arg))                                          \
               : (4 == sizeof(*(_obj)) ? (uint32_t)_InterlockedAnd((volatile long *)(_obj), (long)(_arg))                      \
                                       : (uint64_t)_InterlockedAnd64((volatile __int64 *)(_obj), (__int64)(_arg))))))

#define custom_atomic_fetch_or(_obj, _arg)                                                                                     \
  ((1 == sizeof(*(_obj))                                                                                                       \
        ? (uint8_t)_InterlockedOr8((volatile char *)(_obj), (char)(_arg))                                                      \
        : (2 == sizeof(*(_obj))                                                                                                \
               ? (uint16_t)_InterlockedOr16((volatile short *)(_obj), (short)(_arg))                                           \
               : (4 == sizeof(*(_obj)) ? (uint32_t)_InterlockedOr((volatile long *)(_obj), (long)(_arg))                       \
                                       : (uint64_t)_InterlockedOr64((volatile __int64 *)(_obj), (__int64)(_arg))))))

#define custom_atomic_fetch_xor(_obj, _arg)                                                                                    \
  ((1 == sizeof(*(_obj))                                                                                                       \
        ? (uint8_t)_InterlockedXor8((volatile char *)(_obj), (char)(_arg))                                                     \
        : (2 == sizeof(*(_obj))                                                                                                \
               ? (uint16_t)_InterlockedXor16((volatile short *)(_obj), (short)(_arg))                                          \
               : (4 == sizeof(*(_obj)) ? (uint32_t)_InterlockedXor((volatile long *)(_obj), (long)(_arg))                      \
                                       : (uint64_t)_InterlockedXor64((volatile __int64 *)(_obj), (__int64)(_arg))))))

#else

#define custom_atomic_exchange(_obj, _desired)                                                                                 \
  ((1 == sizeof(*(_obj))                                                                                                       \
        ? (uint8_t)_InterlockedExchange8((volatile char *)(_obj), (uint8_t)(_desired))                                         \
        : (2 == sizeof(*(_obj)) ? (uint16_t)_InterlockedExchange16((volatile short *)(_obj), (short)(_desired))                \
                                : (uint32_t)_InterlockedExchange((volatile long *)(_obj), (long)(_desired)))))

#define custom_atomic_compare_exchange_strong(_obj, _expected, _desired)                                                       \
  ((1 == sizeof(*(_obj))                                                                                                       \
        ? custom_atomic_implementation_compare_exchange_uint8_t((volatile uint8_t *)(_obj), (uint8_t *)(_expected),            \
                                                                (uint8_t)(_desired))                                           \
        : (2 == sizeof(*(_obj)) ? custom_atomic_implementation_compare_exchange_uint16_t(                                      \
                                      (volatile uint16_t *)(_obj), (uint16_t *)(_expected), (uint16_t)(_desired))              \
                                : custom_atomic_implementation_compare_exchange_uint32_t(                                      \
                                      (volatile uint32_t *)(_obj), (uint32_t *)(_expected), (uint32_t)(_desired)))))
#define custom_atomic_compare_exchange_weak custom_atomic_compare_exchange_strong

#define custom_atomic_fetch_add(_obj, _arg)                                                                                    \
  ((1 == sizeof(*(_obj))                                                                                                       \
        ? (uint8_t)_InterlockedExchangeAdd8((volatile char *)(_obj), (char)(_arg))                                             \
        : (2 == sizeof(*(_obj)) ? (uint16_t)_InterlockedExchangeAdd16((volatile short *)(_obj), (short)(_arg))                 \
                                : (uint32_t)_InterlockedExchangeAdd((volatile long *)(_obj), (long)(_arg)))))

#define custom_atomic_fetch_sub(_obj, _arg)                                                                                    \
  ((1 == sizeof(*(_obj))                                                                                                       \
        ? (uint8_t)_InterlockedExchangeAdd8((volatile char *)(_obj), -(char)(_arg))                                            \
        : (2 == sizeof(*(_obj)) ? (uint16_t)_InterlockedExchangeAdd16((volatile short *)(_obj), -(short)(_arg))                \
                                : (uint32_t)_InterlockedExchangeAdd((volatile long *)(_obj), -(long)(_arg)))))

#define custom_atomic_fetch_and(_obj, _arg)                                                                                    \
  ((1 == sizeof(*(_obj)) ? (uint8_t)_InterlockedAnd8((volatile char *)(_obj), (char)(_arg))                                    \
                         : (2 == sizeof(*(_obj)) ? (uint16_t)_InterlockedAnd16((volatile short *)(_obj), (short)(_arg))        \
                                                 : (uint32_t)_InterlockedAnd((volatile long *)(_obj), (long)(_arg)))))

#define custom_atomic_fetch_or(_obj, _arg)                                                                                     \
  ((1 == sizeof(*(_obj)) ? (uint8_t)_InterlockedOr8((volatile char *)(_obj), (char)(_arg))                                     \
                         : (2 == sizeof(*(_obj)) ? (uint16_t)_InterlockedOr16((volatile short *)(_obj), (short)(_arg))         \
                                                 : (uint32_t)_InterlockedOr((volatile long *)(_obj), (long)(_arg)))))

#define custom_atomic_fetch_xor(_obj, _arg)                                                                                    \
  ((1 == sizeof(*(_obj)) ? (uint8_t)_InterlockedXor8((volatile char *)(_obj), (char)(_arg))                                    \
                         : (2 == sizeof(*(_obj)) ? (uint16_t)_InterlockedXor16((volatile short *)(_obj), (short)(_arg))        \
                                                 : (uint32_t)_InterlockedXor((volatile long *)(_obj), (long)(_arg)))))

#endif

static inline bool custom_atomic_implementation_compare_exchange_uint8_t(volatile uint8_t *obj, uint8_t *expected,
                                                                         uint8_t desired) {
  uint8_t arg = *expected;
  uint8_t result = (uint8_t)_InterlockedCompareExchange8((volatile char *)obj, (char)desired, arg);

  if (arg != result) {
    *expected = result;
    return false;
  } else {
    return true;
  }
}
static inline bool custom_atomic_implementation_compare_exchange_uint16_t(volatile uint16_t *obj, uint16_t *expected,
                                                                          uint16_t desired) {
  uint16_t arg = *expected;
  uint16_t result = (uint16_t)_InterlockedCompareExchange16((volatile short *)obj, (short)desired, arg);

  if (arg != result) {
    *expected = result;
    return false;
  } else {
    return true;
  }
}
static inline bool custom_atomic_implementation_compare_exchange_uint32_t(volatile uint32_t *obj, uint32_t *expected,
                                                                          uint32_t desired) {
  uint32_t arg = *expected;
  uint32_t result = (uint8_t)_InterlockedCompareExchange((volatile long *)obj, (long)desired, arg);

  if (arg != result) {
    *expected = result;
    return false;
  } else {
    return true;
  }
}

#if defined(HAS_ATOMIC_64)
static inline bool custom_atomic_implementation_compare_exchange_uint64_t(volatile uint64_t *obj, uint64_t *expected,
                                                                          uint64_t desired) {
  uint64_t arg = *expected;
  uint64_t result = (uint64_t)_InterlockedCompareExchange64((volatile __int64 *)obj, (__int64)desired, arg);

  if (arg != result) {
    *expected = result;
    return false;
  } else {
    return true;
  }
}
#endif /*HAS_ATOMIC_64*/

#endif /*__STDC_NO_ATOMICS__*/

#define custom_atomic_fetch_inc(_obj) custom_atomic_fetch_add((_obj), 1)
#define custom_atomic_fetch_dec(_obj) custom_atomic_fetch_sub((_obj), 1)
