//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_DETAIL_NIKOLAEV_SCQ_HPP
#define XENIUM_DETAIL_NIKOLAEV_SCQ_HPP

#include "xenium/utils.hpp"

#include <atomic>
#include <cassert>
#include <memory>

namespace xenium::detail {

struct nikolaev_scq {
  struct empty_tag {};
  struct full_tag {};
  struct first_used_tag {};
  struct first_empty_tag {};

  nikolaev_scq(std::size_t capacity, std::size_t remap_shift, empty_tag);
  nikolaev_scq(std::size_t capacity, std::size_t remap_shift, full_tag);
  nikolaev_scq(std::size_t capacity, std::size_t remap_shift, first_used_tag);
  nikolaev_scq(std::size_t capacity, std::size_t remap_shift, first_empty_tag);

  template <bool Nonempty, bool Finalizable>
  bool enqueue(std::uint64_t value, std::size_t capacity, std::size_t remap_shift);
  template <bool Nonempty, std::size_t PopRetries>
  bool dequeue(std::uint64_t& value, std::size_t capacity, std::size_t remap_shift);

  void finalize() { _tail.fetch_or(1, std::memory_order_relaxed); }
  void set_threshold(std::int64_t v) { _threshold.store(v, std::memory_order_relaxed); }

  static constexpr std::size_t calc_remap_shift(std::size_t capacity) {
    assert(utils::is_power_of_two(capacity));
    return utils::find_last_bit_set(capacity / indexes_per_cacheline);
  }

private:
  using index_t = std::uint64_t;
  using indexdiff_t = std::int64_t;
  using value_t = std::uint64_t;

  static constexpr std::size_t cacheline_size = 64;
  static constexpr std::size_t indexes_per_cacheline = cacheline_size / sizeof(index_t);

  void catchup(std::uint64_t tail, std::uint64_t head);

  static inline indexdiff_t diff(index_t a, index_t b) { return static_cast<indexdiff_t>(a - b); }

  static inline index_t remap_index(index_t idx, std::size_t remap_shift, std::size_t n) {
    assert(remap_shift == 0 || (1 << remap_shift) * indexes_per_cacheline == n);
    idx >>= 1;
    return ((idx & (n - 1)) >> remap_shift) | ((idx * indexes_per_cacheline) & (n - 1));
  }

  // index values are structured as follows
  // 0..log2(capacity)+1 bits   - value [0..capacity-1, nil (=2*capacity-1)]
  // 1 bit                      - is_safe flag
  // log2(capacity) + 2..n bits - cycle

  std::atomic<index_t> _head;
  alignas(64) std::atomic<std::int64_t> _threshold;
  alignas(64) std::atomic<index_t> _tail;
  alignas(64) std::unique_ptr<std::atomic<std::uint64_t>[]> _data;

  // the LSB is used for finaliziation
  static constexpr index_t finalized = 1;
  static constexpr index_t index_inc = 2;
};

inline nikolaev_scq::nikolaev_scq(std::size_t capacity, std::size_t remap_shift, empty_tag) :
    _head(0),
    _threshold(-1),
    _tail(0),
    _data(new std::atomic<index_t>[capacity * 2]) {
  const auto n = capacity * 2;
  for (std::size_t i = 0; i < n; ++i) {
    _data[remap_index(i << 1, remap_shift, n)].store(static_cast<index_t>(-1), std::memory_order_relaxed);
  }
}

inline nikolaev_scq::nikolaev_scq(std::size_t capacity, std::size_t remap_shift, full_tag) :
    _head(0),
    _threshold(static_cast<std::int64_t>(capacity) * 3 - 1),
    _tail(capacity * index_inc),
    _data(new std::atomic<index_t>[capacity * 2]) {
  const auto n = capacity * 2;
  for (std::size_t i = 0; i < capacity; ++i) {
    _data[remap_index(i << 1, remap_shift, n)].store(n + i, std::memory_order_relaxed);
  }
  for (std::size_t i = capacity; i < n; ++i) {
    _data[remap_index(i << 1, remap_shift, n)].store(static_cast<index_t>(-1), std::memory_order_relaxed);
  }
}

inline nikolaev_scq::nikolaev_scq(std::size_t capacity, std::size_t remap_shift, first_used_tag) :
    _head(0),
    _threshold(static_cast<std::int64_t>(capacity) * 3 - 1),
    _tail(index_inc),
    _data(new std::atomic<index_t>[capacity * 2]) {
  const auto n = capacity * 2;
  _data[remap_index(0, remap_shift, n)].store(n, std::memory_order_relaxed);
  for (std::size_t i = 1; i < n; ++i) {
    _data[remap_index(i << 1, remap_shift, n)].store(static_cast<index_t>(-1), std::memory_order_relaxed);
  }
}

inline nikolaev_scq::nikolaev_scq(std::size_t capacity, std::size_t remap_shift, first_empty_tag) :
    _head(index_inc),
    _threshold(static_cast<std::int64_t>(capacity) * 3 - 1),
    _tail(capacity * index_inc),
    _data(new std::atomic<index_t>[capacity * 2]) {
  const auto n = capacity * 2;
  _data[remap_index(0, remap_shift, n)].store(static_cast<index_t>(-1), std::memory_order_relaxed);
  for (std::size_t i = 1; i < capacity; ++i) {
    _data[remap_index(i << 1, remap_shift, n)].store(n + i, std::memory_order_relaxed);
  }
  for (std::size_t i = capacity; i < n; ++i) {
    _data[remap_index(i << 1, remap_shift, n)].store(static_cast<index_t>(-1), std::memory_order_relaxed);
  }
}

template <bool Nonempty, bool Finalizable>
inline bool nikolaev_scq::enqueue(std::uint64_t value, std::size_t capacity, std::size_t remap_shift) {
  assert(value < capacity);
  const std::size_t n = capacity * 2;
  const std::size_t is_safe_and_value_mask = 2 * n - 1;

  value ^= is_safe_and_value_mask;

  for (;;) {
    auto tail = _tail.fetch_add(index_inc, std::memory_order_relaxed);
    if constexpr (Finalizable) {
      if (tail & finalized) {
        return false;
      }
    }
    if (tail & finalized) {
      assert((tail & finalized) == 0);
    }
    const auto tail_cycle = tail | is_safe_and_value_mask;
    const auto tidx = remap_index(tail, remap_shift, n);
    // (1) - this acquire-load synchronizes-with the release-fetch_or (4) and the release-CAS (5)
    auto entry = _data[tidx].load(std::memory_order_acquire);

  retry:
    const auto entry_cycle = entry | is_safe_and_value_mask;
    if (diff(entry_cycle, tail_cycle) < 0 &&
        (entry == entry_cycle ||
         (entry == (entry_cycle ^ n) && diff(_head.load(std::memory_order_relaxed), tail) <= 0))) {
      // (2) - this release-CAS synchronizes-with the acquire-load (3) and the acquire-CAS (5)
      if (!_data[tidx].compare_exchange_weak(
            entry, tail_cycle ^ value, std::memory_order_release, std::memory_order_relaxed)) {
        goto retry;
      }

      const auto threshold = static_cast<std::int64_t>(n + capacity - 1);
      if constexpr (!Nonempty) {
        if (_threshold.load(std::memory_order_relaxed) != threshold) {
          _threshold.store(threshold, std::memory_order_relaxed);
        }
      }
      return true;
    }
  }
  return true;
}

template <bool Nonempty, std::size_t PopRetries>
inline bool nikolaev_scq::dequeue(std::uint64_t& value, std::size_t capacity, std::size_t remap_shift) {
  if constexpr (!Nonempty) {
    if (_threshold.load(std::memory_order_relaxed) < 0) {
      return false;
    }
  }

  const std::size_t n = capacity * 2;
  const std::size_t value_mask = n - 1;
  const std::size_t is_safe_and_value_mask = 2 * n - 1;

  for (;;) {
    const auto head = _head.fetch_add(index_inc, std::memory_order_relaxed);
    assert((head & finalized) == 0);
    const auto head_cycle = head | is_safe_and_value_mask;
    const auto hidx = remap_index(head, remap_shift, n);
    std::size_t attempt = 0;
    std::uint64_t entry_cycle;
    std::uint64_t entry_new;

  retry:
    // (3) - this acquire-load synchronizes-with the release-CAS (2)
    auto entry = _data[hidx].load(std::memory_order_acquire);
    do {
      entry_cycle = entry | is_safe_and_value_mask;
      if (entry_cycle == head_cycle) {
        // (4) - this release-fetch_or synchronizes-with the acquire-load (1)
        _data[hidx].fetch_or(value_mask, std::memory_order_release);
        value = entry & value_mask;
        assert(value < capacity);
        return true;
      }

      if ((entry | n) != entry_cycle) {
        entry_new = entry & ~n;
        if (entry == entry_new) {
          break;
        }
      } else {
        auto tail = _tail.load(std::memory_order_relaxed);
        if (diff(tail, head + index_inc) > 0 && ++attempt <= PopRetries) {
          goto retry;
        }
        assert((head_cycle & is_safe_and_value_mask) == is_safe_and_value_mask);
        entry_new = head_cycle;
      }
    } while (
      diff(entry_cycle, head_cycle) < 0 &&
      // (5) - in case of success, this release-CAS synchronizes with the acquire-load (1),
      //       in case of failure, this acquire-CAS synchronizes with the release-CAS (2)
      // It would be sufficient to use release for the success order, but this triggers a
      // false positive in TSan (see https://github.com/google/sanitizers/issues/1264)
      !_data[hidx].compare_exchange_weak(entry, entry_new, std::memory_order_acq_rel, std::memory_order_acquire));

    if constexpr (!Nonempty) {
      auto tail = _tail.load(std::memory_order_relaxed);
      if (diff(tail, head + index_inc) <= 0) {
        catchup(tail, head + index_inc);
        _threshold.fetch_sub(1, std::memory_order_relaxed);
        return false;
      }

      if (_threshold.fetch_sub(1, std::memory_order_relaxed) <= 0) {
        return false;
      }
    }
  }
}

inline void nikolaev_scq::catchup(std::uint64_t tail, std::uint64_t head) {
  while (!_tail.compare_exchange_weak(tail, head, std::memory_order_relaxed)) {
    head = _head.load(std::memory_order_relaxed);
    if (diff(tail, head) >= 0) {
      break;
    }
  }
}
} // namespace xenium::detail

#endif
