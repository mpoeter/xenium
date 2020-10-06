//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_DETAILS_HARDWARE_HPP
#define XENIUM_DETAILS_HARDWARE_HPP

#include <xenium/detail/port.hpp>

#if defined(XENIUM_ARCH_X86)
  #include <emmintrin.h>
#elif defined(XENIUM_ARCH_SPARC)
  #include <synch.h>
#endif

namespace xenium::detail {
inline void hardware_pause() {
  // TODO - add pause implementations for ARM + Power
#if defined(XENIUM_ARCH_X86)
  _mm_pause();
#elif defined(XENIUM_ARCH_SPARC)
  smt_pause();
#else
  #warning "No hardware_pause implementation available - falling back to local volatile noop."
  // this effectively prevents the compiler from optimizing away the whole backoff operation
  volatile int x = 0;
  (void)x;
#endif
}
} // namespace xenium::detail
#endif
