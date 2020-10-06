//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_DETAIL_PORT_HPP
#define XENIUM_DETAIL_PORT_HPP

#if !defined(__SANITIZE_THREAD__) && defined(__has_feature)
  #if __has_feature(thread_sanitizer)
    #define __SANITIZE_THREAD__ // NOLINT
  #endif
#endif

#if defined(__SANITIZE_THREAD__)
  #define TSAN_MEMORY_ORDER(tsan_order, _) tsan_order
#else
  #define TSAN_MEMORY_ORDER(_tsan_order, normal_order) normal_order
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

#endif
