//
// Copyright (c) 2018 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_FIXED_SIZE_CIRCULAR_ARRAY_HPP
#define XENIUM_FIXED_SIZE_CIRCULAR_ARRAY_HPP

#include <atomic>

namespace xenium { namespace detail {
  template <class T, unsigned Capacity>
  struct fixed_size_circular_array {
    unsigned capacity() const { return Capacity; }

    T* get(unsigned idx, std::memory_order order) {
      return items[idx & mask].load(order);
    }

    void put(unsigned idx, T* value, std::memory_order order) {
      items[idx & mask].store(value, order);
    }
  private:
    static constexpr unsigned mask = Capacity - 1;
    static_assert((Capacity & mask) == 0, "capacity has to be a power of two");

    std::atomic<T*> items[Capacity];
  };
}}
#endif
