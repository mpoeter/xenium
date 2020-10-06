//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_KIRSCH_BOUNDED_KFIFO_QUEUE_HPP
#define XENIUM_KIRSCH_BOUNDED_KFIFO_QUEUE_HPP

#include <xenium/marked_ptr.hpp>
#include <xenium/parameter.hpp>
#include <xenium/policy.hpp>
#include <xenium/utils.hpp>

#include <xenium/detail/pointer_queue_traits.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <stdexcept>

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable : 26495) // uninitialized member variable
#endif

namespace xenium {
/**
 * @brief A bounded lock-free multi-producer/multi-consumer k-FIFO queue.
 *
 * This is an implementation of the proposal by Kirsch et al. [[KLP13](index.html#ref-kirsch-2013)\].
 *
 * A k-FIFO queue can be understood as a queue where each element may be dequeued
 * out-of-order up to k−1.
 *
 * A limitation of this queue is that it can only handle pointers or trivially copyable types that are
 * smaller than a pointer (i.e., `T` must be a raw pointer, a `std::unique_ptr` or a trivially copyable
 * type like std::uint32_t).
 *
 * Supported policies:
 *  * `xenium::policy::padding_bytes`<br>
 *    Defines the number of padding bytes for each entry. (*optional*; defaults to `sizeof(T*)`)
 *
 * @tparam T
 * @tparam Policies list of policies to customize the behaviour
 */
template <class T, class... Policies>
class kirsch_bounded_kfifo_queue {
private:
  using traits = detail::pointer_queue_traits_t<T, Policies...>;
  using raw_value_type = typename traits::raw_type;

public:
  using value_type = T;
  static constexpr unsigned padding_bytes =
    parameter::value_param_t<unsigned, policy::padding_bytes, sizeof(raw_value_type), Policies...>::value;

  kirsch_bounded_kfifo_queue(uint64_t k, uint64_t num_segments);
  ~kirsch_bounded_kfifo_queue();

  kirsch_bounded_kfifo_queue(const kirsch_bounded_kfifo_queue&) = delete;
  kirsch_bounded_kfifo_queue(kirsch_bounded_kfifo_queue&&) = delete;

  kirsch_bounded_kfifo_queue& operator=(const kirsch_bounded_kfifo_queue&) = delete;
  kirsch_bounded_kfifo_queue& operator=(kirsch_bounded_kfifo_queue&&) = delete;

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

private:
  using marked_value = xenium::marked_ptr<std::remove_pointer_t<raw_value_type>, 16>;

  struct padded_entry {
    std::atomic<marked_value> value;
    // we use max here to avoid arrays of size zero which are not allowed by Visual C++
    char padding[std::max(padding_bytes, 1U)];
  };

  struct unpadded_entry {
    std::atomic<marked_value> value;
  };

  using entry = std::conditional_t<padding_bytes == 0, unpadded_entry, padded_entry>;

public:
  /**
   * @brief Provides the effective size of a single queue entry (including padding).
   */
  static constexpr std::size_t entry_size = sizeof(entry);

private:
  struct marked_idx {
    marked_idx() = default;
    marked_idx(uint64_t val, uint64_t mark) noexcept { _val = val | (mark << bits); }

    [[nodiscard]] uint64_t get() const noexcept { return _val & val_mask; }
    [[nodiscard]] uint64_t mark() const noexcept { return _val >> bits; }
    bool operator==(const marked_idx& other) const noexcept { return this->_val == other._val; }
    bool operator!=(const marked_idx& other) const noexcept { return this->_val != other._val; }

  private:
    static constexpr unsigned bits = 16;
    static constexpr uint64_t val_mask = (static_cast<uint64_t>(1) << bits) - 1;
    uint64_t _val = 0;
  };

  template <bool Empty>
  bool find_index(uint64_t start_index, uint64_t& index, marked_value& old);
  bool queue_full(const marked_idx& head_old, const marked_idx& tail_old) const;
  bool segment_empty(const marked_idx& head_old) const;
  [[nodiscard]] bool not_in_valid_region(uint64_t tail_old, uint64_t tail_current, uint64_t head_current) const;
  [[nodiscard]] bool in_valid_region(uint64_t tail_old, uint64_t tail_current, uint64_t head_current) const;
  bool committed(const marked_idx& tail_old, marked_value new_value, uint64_t index);

  std::uint64_t _queue_size;
  std::size_t _k;
  // all operations on head/tail are synchronized via the value operations and
  // can therefore use memory_order_relaxed.
  std::atomic<marked_idx> _head;
  std::atomic<marked_idx> _tail;
  std::unique_ptr<entry[]> _queue;
};

template <class T, class... Policies>
kirsch_bounded_kfifo_queue<T, Policies...>::kirsch_bounded_kfifo_queue(uint64_t k, uint64_t num_segments) :
    _queue_size(k * num_segments),
    _k(k),
    _head(),
    _tail(),
    _queue(new entry[k * num_segments]()) {}

template <class T, class... Policies>
kirsch_bounded_kfifo_queue<T, Policies...>::~kirsch_bounded_kfifo_queue() {
  for (unsigned i = 0; i < _queue_size; ++i) {
    traits::delete_value(_queue[i].value.load(std::memory_order_relaxed).get());
  }
}

template <class T, class... Policies>
bool kirsch_bounded_kfifo_queue<T, Policies...>::try_push(value_type value) {
  if (value == nullptr) {
    throw std::invalid_argument("value can not be nullptr");
  }

  raw_value_type raw_value = traits::get_raw(value);
  for (;;) {
    marked_idx tail_old = _tail.load(std::memory_order_relaxed);
    marked_idx head_old = _head.load(std::memory_order_relaxed);

    uint64_t idx;
    marked_value old_value;
    bool found_idx = find_index<true>(tail_old.get(), idx, old_value);
    if (tail_old != _tail.load(std::memory_order_relaxed)) {
      continue;
    }

    if (found_idx) {
      assert(old_value.get() == nullptr);
      const marked_value new_value(raw_value, old_value.mark() + 1);
      // (1) - this release-CAS synchronizes with the acquire-load (3, 4)
      if (_queue[idx].value.compare_exchange_strong(
            old_value, new_value, std::memory_order_release, std::memory_order_relaxed) &&
          committed(tail_old, new_value, idx)) {
        traits::release(value);
        return true;
      }
    } else {
      if (queue_full(head_old, tail_old)) {
        if (segment_empty(head_old)) {
          // increment head by k
          marked_idx new_head((head_old.get() + _k) % _queue_size, head_old.mark() + 1);
          _head.compare_exchange_strong(head_old, new_head, std::memory_order_relaxed);
        } else if (head_old == _head.load(std::memory_order_relaxed)) {
          // queue is full
          return false;
        }
      }
      // increment tail by k
      marked_idx new_tail((tail_old.get() + _k) % _queue_size, tail_old.mark() + 1);
      _tail.compare_exchange_strong(tail_old, new_tail, std::memory_order_relaxed);
    }
  }
}

template <class T, class... Policies>
bool kirsch_bounded_kfifo_queue<T, Policies...>::try_pop(value_type& result) {
  for (;;) {
    marked_idx head_old = _head.load(std::memory_order_relaxed);
    marked_idx tail_old = _tail.load(std::memory_order_relaxed);

    uint64_t idx;
    marked_value old_value;
    bool found_idx = find_index<false>(head_old.get(), idx, old_value);
    if (head_old != _head.load(std::memory_order_relaxed)) {
      continue;
    }

    if (found_idx) {
      assert(old_value.get() != nullptr);
      if (head_old.get() == tail_old.get()) {
        marked_idx new_tail((tail_old.get() + _k) % _queue_size, tail_old.mark() + 1);
        _tail.compare_exchange_strong(tail_old, new_tail, std::memory_order_relaxed);
      }
      marked_value new_value(nullptr, old_value.mark() + 1);
      // (2) - this release-CAS synchronizes with the acquire-load (3, 4)
      if (_queue[idx].value.compare_exchange_strong(
            old_value, new_value, std::memory_order_release, std::memory_order_relaxed)) {
        traits::store(result, old_value.get());
        return true;
      }
    } else {
      if (head_old.get() == tail_old.get() && tail_old == _tail.load(std::memory_order_relaxed)) {
        return false;
      }

      marked_idx new_head((head_old.get() + _k) % _queue_size, head_old.mark() + 1);
      _head.compare_exchange_strong(head_old, new_head, std::memory_order_relaxed);
    }
  }
}

template <class T, class... Policies>
template <bool Empty>
bool kirsch_bounded_kfifo_queue<T, Policies...>::find_index(uint64_t start_index,
                                                            uint64_t& value_index,
                                                            marked_value& old) {
  const uint64_t random_index = utils::random() % _k;
  for (size_t i = 0; i < _k; i++) {
    // TODO - this can be simplified if queue_size is a multiple of k!
    uint64_t index = (start_index + ((random_index + i) % _k)) % _queue_size;
    // (3) - this acquire-load synchronizes-with the release-CAS (1, 2)
    old = _queue[index].value.load(std::memory_order_acquire);
    if ((Empty && old.get() == nullptr) || (!Empty && old.get() != nullptr)) {
      value_index = index;
      return true;
    }
  }
  return false;
}

template <class T, class... Policies>
bool kirsch_bounded_kfifo_queue<T, Policies...>::committed(const marked_idx& tail_old,
                                                           marked_value value,
                                                           uint64_t index) {
  if (_queue[index].value.load(std::memory_order_relaxed) != value) {
    return true;
  }

  marked_idx tail_current = _tail.load(std::memory_order_relaxed);
  marked_idx head_current = _head.load(std::memory_order_relaxed);
  if (in_valid_region(tail_old.get(), tail_current.get(), head_current.get())) {
    return true;
  }

  if (not_in_valid_region(tail_old.get(), tail_current.get(), head_current.get())) {
    marked_value new_value(nullptr, value.mark() + 1);
    if (!_queue[index].value.compare_exchange_strong(value, new_value, std::memory_order_relaxed)) {
      return true;
    }
  } else {
    marked_idx new_head(head_current.get(), head_current.mark() + 1);
    if (_head.compare_exchange_strong(head_current, new_head, std::memory_order_relaxed)) {
      return true;
    }

    marked_value new_value(nullptr, value.mark() + 1);
    if (!_queue[index].value.compare_exchange_strong(value, new_value, std::memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

template <class T, class... Policies>
bool kirsch_bounded_kfifo_queue<T, Policies...>::queue_full(const marked_idx& head_old,
                                                            const marked_idx& tail_old) const {
  return (((tail_old.get() + _k) % _queue_size) == head_old.get() &&
          (head_old == _head.load(std::memory_order_relaxed)));
}

template <class T, class... Policies>
bool kirsch_bounded_kfifo_queue<T, Policies...>::segment_empty(const marked_idx& head_old) const {
  const uint64_t start = head_old.get();
  for (size_t i = 0; i < _k; i++) {
    // TODO - this can be simplified if queue_size is a multiple of k!
    // (4) - this acquire-load synchronizes-with the release-CAS (1, 2)
    if (_queue[(start + i) % _queue_size].value.load(std::memory_order_acquire).get() != nullptr) {
      return false;
    }
  }
  return true;
}

template <class T, class... Policies>
bool kirsch_bounded_kfifo_queue<T, Policies...>::in_valid_region(uint64_t tail_old,
                                                                 uint64_t tail_current,
                                                                 uint64_t head_current) const {
  bool wrap_around = tail_current < head_current;
  if (!wrap_around) {
    return head_current < tail_old && tail_old <= tail_current;
  }
  return head_current < tail_old || tail_old <= tail_current;
}

template <class T, class... Policies>
bool kirsch_bounded_kfifo_queue<T, Policies...>::not_in_valid_region(uint64_t tail_old,
                                                                     uint64_t tail_current,
                                                                     uint64_t head_current) const {
  bool wrap_around = tail_current < head_current;
  if (!wrap_around) {
    return tail_old < tail_current || head_current < tail_old;
  }
  return tail_old < tail_current && head_current < tail_old;
}
} // namespace xenium
#ifdef _MSC_VER
  #pragma warning(pop)
#endif

#endif
