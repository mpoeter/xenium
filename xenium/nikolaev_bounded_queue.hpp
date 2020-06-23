//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_NIKOLAEV_BOUNDED_QUEUE_HPP
#define XENIUM_NIKOLAEV_BOUNDED_QUEUE_HPP

#include <xenium/parameter.hpp>
#include <xenium/policy.hpp>
#include <xenium/utils.hpp>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>

namespace xenium {
  /**
   * @brief A bounded lock-free multi-producer/multi-consumer queue.
   * 
   * This implementation is based on the bounded MPMC queue proposed by Nikolaev
   * \[[Nik19](index.html#ref-nikolaev-2019)\].
   * 
   * The nikoleav_bounded_queue provides lock-free progress guarantee under the condition that
  *  the number of threads concurrently operating on the queue is less than the queue's capacity.
   *
   * Supported policies:
   *  * `xenium::policy::pop_retries`<br>
   *    Defines the number of iterations to spin on a queue entry while waiting for a pending
   *    push operation to finish. (*optional*; defaults to 1000)
   *    Note: this policy is applied to the _internal_ queues. The Nikolaev queue internally
   *    uses two queues to manage the indexes of free/allocated slots. A push operation pops an
   *    item from the free queue, so this policy actually affects both, push and pop operations.
   *
   * @tparam T
   * @tparam Policies list of policies to customize the behaviour
   */
  template <class T, class... Policies>
  class nikolaev_bounded_queue {
  public:
    using value_type = T;
    static constexpr unsigned pop_retries =
      parameter::value_param_t<unsigned, policy::pop_retries, 1000, Policies...>::value;

    /**
     * @brief Constructs a new instance with the specified maximum size.
     * @param capacity max number of elements in the queue; If this is not a power of two,
     * it will be rounded to the next power of two
     */
    nikolaev_bounded_queue(std::size_t capacity);
    ~nikolaev_bounded_queue() = default;

    nikolaev_bounded_queue(const nikolaev_bounded_queue&) = delete;
    nikolaev_bounded_queue(nikolaev_bounded_queue&&) = delete;

    nikolaev_bounded_queue& operator= (const nikolaev_bounded_queue&) = delete;
    nikolaev_bounded_queue& operator= (nikolaev_bounded_queue&&) = delete;

    /**
     * @brief Tries to push a new element to the queue.
     *
     * Progress guarantees: lock-free
     *
     * @param value
     * @return `true` if the operation was successful, otherwise `false`
     */
    bool try_push(value_type value);

    /**
     * @brief Tries to pop an element from the queue.
     *
     * Progress guarantees: lock-free
     *
     * @param result
     * @return `true` if the operation was successful, otherwise `false`
     */
    bool try_pop(value_type& result);

    /**
     * @brief Returns the (rounded) capacity of the queue.
     */
    std::size_t capacity() const noexcept { return _capacity; }

  private:
    struct empty_tag{};
    struct full_tag{};

    using index_t = std::uint64_t;
    static constexpr std::size_t cacheline_size = 64;
    static constexpr std::size_t indexes_per_cacheline = cacheline_size / sizeof(index_t);

    struct queue {
      queue(std::size_t capacity, std::size_t remap_shift, empty_tag);
      queue(std::size_t capacity, std::size_t remap_shift, full_tag);

      // TODO - make nonempty checks configurable
      void enqueue(std::uint64_t value, std::size_t capacity, std::size_t remap_shift, bool nonempty);
      bool dequeue(std::uint64_t& value, std::size_t capacity, std::size_t remap_shift, bool nonempty);
    private:
      using indexdiff_t = std::int64_t;
      using value_t = std::uint64_t;
      void catchup(std::uint64_t tail, std::uint64_t head);

      static inline indexdiff_t diff(index_t a, index_t b) { 
        return static_cast<indexdiff_t>(a - b);
      }

      static inline index_t remap_index(index_t idx, std::size_t remap_shift, std::size_t n) {
        assert(remap_shift == 0 || (1 << remap_shift) * indexes_per_cacheline == n);
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
    };

    static std::size_t calc_remap_shift(std::size_t capacity) {
      assert(utils::is_power_of_two(capacity));
      return utils::find_last_bit_set(capacity / indexes_per_cacheline);
    }

    const std::size_t _capacity;
    const std::size_t _remap_shift;
    std::unique_ptr<T[]> _data;
    queue _allocated_queue;
    queue _free_queue;
  };

  template <class T, class... Policies>
  nikolaev_bounded_queue<T, Policies...>::nikolaev_bounded_queue(std::size_t capacity) :
    _capacity(utils::next_power_of_two(capacity)),
    _remap_shift(calc_remap_shift(_capacity)),
    _data(new T[_capacity]),
    _allocated_queue(_capacity, _remap_shift, empty_tag{}),
    _free_queue(_capacity, _remap_shift, full_tag{})
  {
    assert(capacity > 0);
    assert(_capacity <= indexes_per_cacheline || _remap_shift > 0);
  }
  
  template <class T, class... Policies>
  bool nikolaev_bounded_queue<T, Policies...>::try_push(value_type value) {
    std::size_t eidx;
    if (!_free_queue.dequeue(eidx, _capacity, _remap_shift, false))
      return false;

    assert(eidx < _capacity);
    _data[eidx] = std::move(value);
    _allocated_queue.enqueue(eidx, _capacity, _remap_shift, false);
    return true;
  }

  template <class T, class... Policies>
  bool nikolaev_bounded_queue<T, Policies...>::try_pop(value_type& result) {
    std::size_t eidx;
    if (!_allocated_queue.dequeue(eidx, _capacity, _remap_shift, false))
      return false;

    assert(eidx < _capacity);
    result = std::move(_data[eidx]);
    _free_queue.enqueue(eidx, _capacity, _remap_shift, false);
    return true;
  }

  template <class T, class... Policies>
  nikolaev_bounded_queue<T, Policies...>::queue::queue(std::size_t capacity, std::size_t remap_shift, empty_tag) :
    _head(0),
    _threshold(-1),
    _tail(0),
    _data(new std::atomic<index_t>[capacity * 2])
  {
    const auto n = capacity * 2;
    for (std::size_t i = 0; i < n; ++i)
      _data[remap_index(i, remap_shift, n)].store(-1, std::memory_order_relaxed);
  }

  template <class T, class... Policies>
  nikolaev_bounded_queue<T, Policies...>::queue::queue(std::size_t capacity, std::size_t remap_shift, full_tag) :
    _head(0),
    _threshold(capacity * 3 - 1),
    _tail(capacity),
    _data(new std::atomic<index_t>[capacity * 2])
  {
    const auto n = capacity * 2;    
    for (std::size_t i = 0; i < capacity; ++i)
      _data[remap_index(i, remap_shift, n)].store(n + i, std::memory_order_relaxed);
    for (std::size_t i = capacity; i < n; ++i)
      _data[remap_index(i, remap_shift, n)].store(-1, std::memory_order_relaxed);
  }

  template <class T, class... Policies>
  void nikolaev_bounded_queue<T, Policies...>::queue::enqueue(std::uint64_t value,
      std::size_t capacity, std::size_t remap_shift, bool nonempty) {
    assert(value < capacity);
    const std::size_t n = capacity * 2;
    const std::size_t is_safe_and_value_mask = 2 * n -1;

    value ^= is_safe_and_value_mask;

    for (;;) {
      const auto tail = _tail.fetch_add(1, std::memory_order_relaxed);
      const auto tail_cycle = (tail << 1) | is_safe_and_value_mask;
      const auto tidx = remap_index(tail, remap_shift, n);
      // (1) - this acquire-load synchronizes-with the release-fetch_or (4) and the release-CAS (5)
      auto entry = _data[tidx].load(std::memory_order_acquire);

    retry:
      const auto entry_cycle = entry | is_safe_and_value_mask;
      if (diff(entry_cycle, tail_cycle) < 0 &&
          (entry == entry_cycle ||
            (entry == (entry_cycle ^ n) &&
             diff(_head.load(std::memory_order_relaxed), tail) <= 0))) {

        // (2) - this release-CAS synchronizes-with the acquire-load (3) and the acquire-CAS (5)
        if (!_data[tidx].compare_exchange_weak(
            entry, tail_cycle ^ value, std::memory_order_release, std::memory_order_relaxed))
          goto retry;

        const std::int64_t threshold = n + capacity -1;
        if (!nonempty && (_threshold.load(std::memory_order_relaxed) != threshold))
          _threshold.store(threshold, std::memory_order_relaxed);
        return;
      }
    }
  }

  template <class T, class... Policies>
  bool nikolaev_bounded_queue<T, Policies...>::queue::dequeue(std::uint64_t& value,
      std::size_t capacity,  std::size_t remap_shift, bool nonempty) {
    if (!nonempty && _threshold.load(std::memory_order_relaxed) < 0) {
      return false;
    }

    const std::size_t n = capacity * 2;
    const std::size_t value_mask = n - 1;
    const std::size_t is_safe_and_value_mask = 2 * n -1;

    for (;;) {
      const auto head = _head.fetch_add(1, std::memory_order_relaxed);
      const auto head_cycle = (head << 1) | is_safe_and_value_mask;
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
          if (entry == entry_new)
            break;
        } else {
          auto tail = _tail.load(std::memory_order_relaxed);
          if (diff(tail, head + 1) > 0 && ++attempt <= pop_retries) {
            goto retry;
          }
          assert((head_cycle & is_safe_and_value_mask) == is_safe_and_value_mask);
          entry_new = head_cycle;
        }
      } while (diff(entry_cycle, head_cycle) < 0 &&
           // (5) - in case of success, this release-CAS synchronizes with the acquire-load (1),
           //       in case of failure, this acquire-CAS synchronizes with the release-CAS (2)
           // It would be sufficient to use release for the success order, but this triggers a
           // false positive in TSan (see https://github.com/google/sanitizers/issues/1264)
          _data[hidx].compare_exchange_weak(entry, entry_new, std::memory_order_acq_rel, std::memory_order_acquire) == false);

      if (!nonempty) {
        auto tail = _tail.load(std::memory_order_relaxed);
        if (diff(tail, head + 1) <= 0) {
          catchup(tail, head + 1);
          _threshold.fetch_sub(1, std::memory_order_relaxed);
          return false;
        }

        if (_threshold.fetch_sub(1, std::memory_order_relaxed) <= 0)
          return false;
      }
    }
  }

  template <class T, class... Policies>
  void nikolaev_bounded_queue<T, Policies...>::queue::catchup(std::uint64_t tail, std::uint64_t head) {
    while (!_tail.compare_exchange_weak(tail, head, std::memory_order_relaxed)) {
      head = _head.load(std::memory_order_relaxed);
      if (diff(tail, head) >= 0)
        break;
    }
  }
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif