//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_KIRSCH_KFIFO_QUEUE_HPP
#define XENIUM_KIRSCH_KFIFO_QUEUE_HPP

#include <xenium/marked_ptr.hpp>
#include <xenium/parameter.hpp>
#include <xenium/policy.hpp>
#include <xenium/utils.hpp>

#include <xenium/detail/pointer_queue_traits.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <stdexcept>

namespace xenium {
/**
 * @brief An unbounded lock-free multi-producer/multi-consumer k-FIFO queue.
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
 *  * `xenium::policy::reclaimer`<br>
 *    Defines the reclamation scheme to be used for internal nodes. (**required**)
 *  * `xenium::policy::padding_bytes`<br>
 *    Defines the number of padding bytes for each entry. (*optional*; defaults to `sizeof(T*)`)
 *
 * @tparam T
 * @tparam Policies list of policies to customize the behaviour
 */
template <class T, class... Policies>
class kirsch_kfifo_queue {
private:
  using traits = detail::pointer_queue_traits_t<T, Policies...>;
  using raw_value_type = typename traits::raw_type;

public:
  using value_type = T;
  using reclaimer = parameter::type_param_t<policy::reclaimer, parameter::nil, Policies...>;
  static constexpr unsigned padding_bytes =
    parameter::value_param_t<unsigned, policy::padding_bytes, sizeof(raw_value_type), Policies...>::value;

  static_assert(parameter::is_set<reclaimer>::value, "reclaimer policy must be specified");

  template <class... NewPolicies>
  using with = kirsch_kfifo_queue<T, NewPolicies..., Policies...>;

  explicit kirsch_kfifo_queue(uint64_t k);
  ~kirsch_kfifo_queue();

  kirsch_kfifo_queue(const kirsch_kfifo_queue&) = delete;
  kirsch_kfifo_queue(kirsch_kfifo_queue&&) = delete;

  kirsch_kfifo_queue& operator=(const kirsch_kfifo_queue&) = delete;
  kirsch_kfifo_queue& operator=(kirsch_kfifo_queue&&) = delete;

  /**
   * @brief Pushes the given value to the queue.
   *
   * This operation might have to allocate a new segment.
   * Progress guarantees: lock-free (may perform a memory allocation)
   *
   * @param value
   */
  void push(value_type value);

  /**
   * @brief Tries to pop an object from the queue.
   *
   * Progress guarantees: lock-free
   *
   * @param result
   * @return `true` if the operation was successful, otherwise `false`
   */
  [[nodiscard]] bool try_pop(value_type& result);

private:
  using marked_value = xenium::marked_ptr<std::remove_pointer_t<raw_value_type>, 16>;

  struct padded_entry {
    std::atomic<marked_value> value;
    // we use max here to avoid arrays of size zero which are not allowed by Visual C++
    char padding[std::max(padding_bytes, 1u)];
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
  struct segment;

  struct segment_deleter {
    void operator()(segment* seg) const { release_segment(seg); }
  };
  struct segment : reclaimer::template enable_concurrent_ptr<segment, 16, segment_deleter> {
    using concurrent_ptr = typename reclaimer::template concurrent_ptr<segment, 16>;

    explicit segment(uint64_t k) : k(k) {}
    ~segment() override {
      for (unsigned i = 0; i < k; ++i) {
        assert(items()[i].value.load(std::memory_order_relaxed).get() == nullptr);
      }
    }

    void delete_remaining_items() {
      for (unsigned i = 0; i < k; ++i) {
        traits::delete_value(items()[i].value.load(std::memory_order_relaxed).get());
        items()[i].value.store(nullptr, std::memory_order_relaxed);
      }
    }

    entry* items() noexcept { return reinterpret_cast<entry*>(this + 1); }

    std::atomic<bool> deleted{false};
    const uint64_t k;
    concurrent_ptr next{};
  };

  using concurrent_ptr = typename segment::concurrent_ptr;
  using marked_ptr = typename concurrent_ptr::marked_ptr;
  using guard_ptr = typename concurrent_ptr::guard_ptr;

  segment* alloc_segment() const;
  static void release_segment(segment* seg);

  template <bool Empty>
  bool find_index(marked_ptr segment, uint64_t& value_index, marked_value& old) const noexcept;
  void advance_head(guard_ptr& head_current, marked_ptr tail_current) noexcept;
  void advance_tail(marked_ptr tail_current) noexcept;
  bool committed(marked_ptr segment, marked_value value, uint64_t index) noexcept;

  const std::size_t k_;
  concurrent_ptr head_;
  concurrent_ptr tail_;
};

template <class T, class... Policies>
kirsch_kfifo_queue<T, Policies...>::kirsch_kfifo_queue(uint64_t k) : k_(k) {
  const auto seg = alloc_segment();
  head_.store(seg, std::memory_order_relaxed);
  tail_.store(seg, std::memory_order_relaxed);
}

template <class T, class... Policies>
kirsch_kfifo_queue<T, Policies...>::~kirsch_kfifo_queue() {
  auto seg = head_.load(std::memory_order_relaxed).get();
  while (seg) {
    auto next = seg->next.load(std::memory_order_relaxed).get();
    seg->delete_remaining_items();
    release_segment(seg);
    seg = next;
  }
}

template <class T, class... Policies>
auto kirsch_kfifo_queue<T, Policies...>::alloc_segment() const -> segment* {
  void* data = ::operator new(sizeof(segment) + k_ * sizeof(entry));
  auto result = new (data) segment(k_);
  for (std::size_t i = 0; i < k_; ++i) {
    new (&result->items()[i]) entry();
  }
  return result;
}

template <class T, class... Policies>
void kirsch_kfifo_queue<T, Policies...>::release_segment(segment* seg) {
  seg->~segment();
  ::operator delete(seg);
}

template <class T, class... Policies>
void kirsch_kfifo_queue<T, Policies...>::push(value_type value) {
  if (value == nullptr) {
    throw std::invalid_argument("value cannot be nullptr");
  }

  raw_value_type raw_value = traits::get_raw(value);
  guard_ptr tail_old;
  for (;;) {
    // (1) - this acquire-load synchronizes-with the release-CAS (9, 12, 14)
    tail_old.acquire(tail_, std::memory_order_acquire);

    // TODO - local linearizability

    uint64_t idx = 0;
    marked_value old_value;
    bool found_idx = find_index<true>(tail_old, idx, old_value);
    if (tail_old != tail_.load(std::memory_order_relaxed)) {
      continue;
    }

    if (found_idx) {
      const marked_value new_value(raw_value, old_value.mark() + 1);
      // (2) - this release-CAS synchronizes-with the acquire-CAS (5)
      if (tail_old->items()[idx].value.compare_exchange_strong(
            old_value, new_value, std::memory_order_release, std::memory_order_relaxed) &&
          committed(tail_old, new_value, idx)) {
        traits::release(value);
        // TODO - local linearizability
        return;
      }
    } else {
      advance_tail(tail_old);
    }
  }
}

template <class T, class... Policies>
bool kirsch_kfifo_queue<T, Policies...>::try_pop(value_type& result) {
  guard_ptr head_old;
  for (;;) {
    // (3) - this acquire-load synchronizes-with the release-CAS (10)
    head_old.acquire(head_, std::memory_order_acquire);
    auto h = head_old.get();
    (void)h;
    uint64_t idx = 0;
    marked_value old_value;
    bool found_idx = find_index<false>(head_old, idx, old_value);
    if (head_old != head_.load(std::memory_order_relaxed)) {
      continue;
    }

    // (4) - this acquire-load synchronizes-with the release-CAS (9, 12, 14)
    marked_ptr tail_old = tail_.load(std::memory_order_acquire);
    if (found_idx) {
      assert(old_value.get() != (void*)0x100);
      if (head_old.get() == tail_old.get()) {
        advance_tail(tail_old);
      }

      const marked_value new_value(nullptr, old_value.mark() + 1);
      // (5) - this acquire-CAS synchronizes-with the release-CAS (2)
      if (head_old->items()[idx].value.compare_exchange_strong(
            old_value, new_value, std::memory_order_acquire, std::memory_order_relaxed)) {
        traits::store(result, old_value.get());
        return true;
      }
    } else {
      if (head_old.get() == tail_old.get() && tail_old == tail_.load(std::memory_order_relaxed)) {
        return false; // queue is empty
      }
      advance_head(head_old, tail_old);
    }
  }
}

template <class T, class... Policies>
template <bool Empty>
bool kirsch_kfifo_queue<T, Policies...>::find_index(marked_ptr segment,
                                                    uint64_t& value_index,
                                                    marked_value& old) const noexcept {
  const uint64_t k = segment->k;
  const uint64_t random_index = utils::random() % k;
  for (size_t i = 0; i < k; i++) {
    uint64_t index = ((random_index + i) % k);
    old = segment->items()[index].value.load(std::memory_order_relaxed);
    if ((Empty && old.get() == nullptr) || (!Empty && old.get() != nullptr)) {
      value_index = index;
      return true;
    }
  }
  return false;
}

template <class T, class... Policies>
bool kirsch_kfifo_queue<T, Policies...>::committed(marked_ptr segment, marked_value value, uint64_t index) noexcept {
  if (value != segment->items()[index].value.load(std::memory_order_relaxed)) {
    return true;
  }

  const marked_value empty_value(nullptr, value.mark() + 1);

  if (segment->deleted.load(std::memory_order_relaxed)) {
    // Insert tail segment has been removed, but we are fine if element still has been removed.
    return !segment->items()[index].value.compare_exchange_strong(value, empty_value, std::memory_order_relaxed);
  }

  // (6) - this acquire-load synchronizes-with the release-CAS (10)
  marked_ptr head_current = head_.load(std::memory_order_acquire);
  if (segment.get() == head_current.get()) {
    // Insert tail segment is now head.
    marked_ptr new_head(head_current.get(), head_current.mark() + 1);
    // This relaxed-CAS is part of a release sequence headed by (10)
    if (head_.compare_exchange_strong(head_current, new_head, std::memory_order_relaxed)) {
      // We are fine if we can update head and thus fail any concurrent
      // advance_head attempts.
      return true;
    }

    // We are fine if element still has been removed.
    return !segment->items()[index].value.compare_exchange_strong(value, empty_value, std::memory_order_relaxed);
  }

  if (!segment->deleted.load(std::memory_order_relaxed)) {
    // Insert tail segment still not deleted.
    return true;
  }
  // Head and tail moved beyond this segment. Try to remove the item.
  // We are fine if element still has been removed.
  return !segment->items()[index].value.compare_exchange_strong(value, empty_value, std::memory_order_relaxed);
}

template <class T, class... Policies>
void kirsch_kfifo_queue<T, Policies...>::advance_head(guard_ptr& head_current, marked_ptr tail_current) noexcept {
  // (7) - this acquire-load synchronizes-with the release-CAS (13)
  const marked_ptr head_next_segment = head_current->next.load(std::memory_order_acquire);
  if (head_current != head_.load(std::memory_order_relaxed)) {
    return;
  }

  if (head_current.get() == tail_current.get()) {
    // (8) - this acquire-load synchronizes-with the release-CAS (13)
    const marked_ptr tail_next_segment = tail_current->next.load(std::memory_order_acquire);
    if (tail_next_segment.get() == nullptr) {
      return;
    }

    if (tail_current == tail_.load(std::memory_order_relaxed)) {
      marked_ptr new_tail(tail_next_segment.get(), tail_current.mark() + 1);
      // (9) - this release-CAS synchronizes-with the acquire-load (1, 4)
      tail_.compare_exchange_strong(tail_current, new_tail, std::memory_order_release, std::memory_order_relaxed);
    }
  }

  head_current->deleted.store(true, std::memory_order_relaxed);

  marked_ptr expected = head_current;
  marked_ptr new_head(head_next_segment.get(), head_current.mark() + 1);
  // (10) - this release-CAS synchronizes-with the acquire-load (3, 6)
  if (head_.compare_exchange_strong(expected, new_head, std::memory_order_release, std::memory_order_relaxed)) {
    head_current.reclaim();
  }
}

template <class T, class... Policies>
void kirsch_kfifo_queue<T, Policies...>::advance_tail(marked_ptr tail_current) noexcept {
  // (11) - this acquire-load synchronizes-with the release-CAS (13)
  marked_ptr next_segment = tail_current->next.load(std::memory_order_acquire);
  if (tail_current != tail_.load(std::memory_order_relaxed)) {
    return;
  }

  if (next_segment.get() != nullptr) {
    marked_ptr new_tail(next_segment.get(), next_segment.mark() + 1);
    // (12) - this release-CAS synchronizes-with the acquire-load (1, 4)
    tail_.compare_exchange_strong(tail_current, new_tail, std::memory_order_release, std::memory_order_relaxed);
  } else {
    auto seg = alloc_segment();
    const marked_ptr new_segment(seg, next_segment.mark() + 1);
    // TODO - insert own value to simplify push?
    // (13) - this release-CAS synchronizes-with the acquire-load (7, 8, 11)
    if (tail_current->next.compare_exchange_strong(
          next_segment, new_segment, std::memory_order_release, std::memory_order_relaxed)) {
      marked_ptr new_tail(seg, tail_current.mark() + 1);
      // (14) - this release-CAS synchronizes-with the acquire-load (1, 4)
      tail_.compare_exchange_strong(tail_current, new_tail, std::memory_order_release, std::memory_order_relaxed);
    } else {
      release_segment(seg);
    }
  }
}
} // namespace xenium
#endif
