//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_DETAIL_PORT_HPP
#define XENIUM_DETAIL_PORT_HPP

#if __SANITIZE_THREAD__
  #define XENIUM_TSAN 1
#elif defined __has_feature
  #if __has_feature(thread_sanitizer)
    #define XENIUM_TSAN 1
  #endif
#endif

#if defined(XENIUM_TSAN)
  // TSan does not support fences, so we have to use other means to establish the necessary synchronization.
  #define TSAN_MEMORY_ORDER(tsan_order, _) tsan_order
  #ifdef __clang__
    #define XENIUM_THREAD_FENCE(memory_order) std::atomic_thread_fence(memory_order);
  #elif defined(__GNUC__)
    #define XENIUM_THREAD_FENCE(memory_order)       \
      _Pragma("GCC diagnostic push");               \
      _Pragma("GCC diagnostic ignored \"-Wtsan\""); \
      std::atomic_thread_fence(memory_order);       \
      _Pragma("GCC diagnostic pop")
  #else
    #error "Unsupported compiler"
  #endif
#else
  #define TSAN_MEMORY_ORDER(_tsan_order, normal_order) normal_order
  #define XENIUM_THREAD_FENCE(memory_order) std::atomic_thread_fence(memory_order)
#endif

#if !defined(XENIM_FORCEINLINE)
  #if defined(_MSC_VER)
    #define XENIUM_FORCEINLINE __forceinline
  #elif defined(__GNUC__) && __GNUC__ > 3
    #define XENIUM_FORCEINLINE inline __attribute__((__always_inline__))
  #else
    #define XENIUM_FORCEINLINE inline
  #endif
#endif

#if !defined(XENIUM_NOINLINE)
  #if defined(_MSC_VER)
    #define XENIUM_NOINLINE __declspec(noinline)
  #elif defined(__GNUC__) && __GNUC__ > 3
    #define XENIUM_NOINLINE __attribute__((__noinline__))
  #else
    #define XENIUM_NOINLINE
  #endif
#endif

#if defined(__has_builtin)
  #if __has_builtin(__builtin_expect)
    #define XENIUM_LIKELY(x) __builtin_expect(x, 1)
    #define XENIUM_UNLIKELY(x) __builtin_expect(x, 0)
  #endif
#endif

#if !defined(XENIUM_LIKELY) || !defined(XENIUM_UNLIKELY)
  #define XENIUM_LIKELY(x) x
  #define XENIUM_UNLIKELY(x) x
#endif

#if !defined(XENIUM_ARCH_X86) && (defined(__x86_64__) || defined(_M_AMD64))
  #define XENIUM_ARCH_X86
#endif

#if !defined(XENIUM_ARCH_SPARC) && defined(__sparc__)
  #define XENIUM_ARCH_SPARC
#endif

#if !defined(XENIUM_ARCH_ARM) && defined(__aarch64__)
  #define XENIUM_ARCH_ARM
#endif

#endif
