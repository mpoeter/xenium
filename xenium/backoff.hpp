//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_BACKOFF_HPP
#define XENIUM_BACKOFF_HPP

#include <algorithm>
#include <xenium/detail/hardware.hpp>

namespace xenium {
/**
 * @brief Dummy backoff strategy that does nothing.
 */
struct no_backoff {
  void operator()() {}
};

/**
 * @brief Simple backoff strategy that always perfoms a single `hardware_pause` operation.
 */
struct single_backoff {
  void operator()() { detail::hardware_pause(); }
};

template <unsigned Max>
struct exponential_backoff {
  static_assert(Max > 0, "Max must be greater than zero. If you don't want to backoff use the `no_backoff` class.");

  void operator()() {
    for (unsigned i = 0; i < count; ++i) {
      detail::hardware_pause();
    }
    count = std::min(Max, count * 2);
  }

private:
  unsigned count = 1;
};

} // namespace xenium

#endif
