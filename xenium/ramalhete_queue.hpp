//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_RAMALHETE_QUEUE_HPP
#define XENIUM_RAMALHETE_QUEUE_HPP

#include <xenium/acquire_guard.hpp>
#include <xenium/backoff.hpp>
#include <xenium/detail/pointer_queue_traits.hpp>
#include <xenium/marked_ptr.hpp>
#include <xenium/parameter.hpp>
#include <xenium/policy.hpp>

#include <algorithm>
#include <atomic>
#include <stdexcept>

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif

namespace xenium {

/**
 * @brief A fast unbounded lock-free multi-producer/multi-consumer FIFO queue.
 *
 * This is an implementation of the `FAAArrayQueue` by Ramalhete and Correia \[[Ram16](index.html#ref-ramalhete-2016)\].
 *
 * It is faster and more efficient than the `michael_scott_queue`, but less generic as it can
 * only handle pointers or trivially copyable types that are smaller than a pointer
 * (i.e., `T` must be a raw pointer, a `std::unique_ptr` or a trivially copyable type like std::uint32_t).
 * Note: `std::unique_ptr` are supported for convinience, but custom deleters are not yet supported.
 *
 * A generic version that does not have this limitation is planned for a future version.
 *
 * Supported policies:
 *  * `xenium::policy::reclaimer`<br>
 *    Defines the reclamation scheme to be used for internal nodes. (**required**)
 *  * `xenium::policy::backoff`<br>
 *    Defines the backoff strategy. (*optional*; defaults to `xenium::no_backoff`)
 *  * `xenium::policy::entries_per_node`<br>
 *    Defines the number of entries for each internal node. It is recommended to make this a
 *    power of two. (*optional*; defaults to 512)
 *  * `xenium::policy::pop_retries`<br>
 *    Defines the number of iterations to spin on a queue entry while waiting for a pending
 *    push operation to finish. (*optional*; defaults to 1000)
 *
 * @tparam T
 * @tparam Policies list of policies to customize the behaviour
 */
template <class T, class... Policies>
class ramalhete_queue {
private:
  using traits = detail::pointer_queue_traits_t<T, Policies...>;
  using raw_value_type = typename traits::raw_type;

public:
  using value_type = T;
  using reclaimer = parameter::type_param_t<policy::reclaimer, parameter::nil, Policies...>;
  using backoff = parameter::type_param_t<policy::backoff, no_backoff, Policies...>;
  static constexpr unsigned entries_per_node =
    parameter::value_param_t<unsigned, policy::entries_per_node, 512, Policies...>::value;
  static constexpr unsigned pop_retries =
    parameter::value_param_t<unsigned, policy::pop_retries, 1000, Policies...>::value;

  static_assert(entries_per_node > 0, "entries_per_node must be greater than zero");
  static_assert(parameter::is_set<reclaimer>::value, "reclaimer policy must be specified");

  template <class... NewPolicies>
  using with = ramalhete_queue<T, NewPolicies..., Policies...>;

  ramalhete_queue();
  ~ramalhete_queue();

  /**
   * @brief Pushes the given value to the queue.
   *
   * This operation might have to allocate a new node.
   * Progress guarantees: lock-free (may perform a memory allocation)
   * @param value
   */
  void push(value_type value);

  /**
   * @brief Tries to pop an object from the queue.
   *
   * Progress guarantees: lock-free
   * @param result
   * @return `true` if the operation was successful, otherwise `false`
   */
  [[nodiscard]] bool try_pop(value_type& result);

private:
  struct node;

  using concurrent_ptr = typename reclaimer::template concurrent_ptr<node, 0>;
  using marked_ptr = typename concurrent_ptr::marked_ptr;
  using guard_ptr = typename concurrent_ptr::guard_ptr;

  // TODO - use type from traits
  using marked_value = xenium::marked_ptr<std::remove_pointer_t<raw_value_type>, 1>;

  struct entry {
    std::atomic<marked_value> value;
  };

  // TODO - make this configurable via policy.
  static constexpr unsigned step_size = 11;
  static constexpr unsigned max_idx = step_size * entries_per_node;

  struct node : reclaimer::template enable_concurrent_ptr<node> {
    // pop_idx and push_idx are incremented by step_size to avoid false sharing, so the
    // actual index has to be calculated modulo entries_per_node
    std::atomic<unsigned> pop_idx;
    entry entries[entries_per_node];
    std::atomic<unsigned> push_idx;
    concurrent_ptr next;

    // Start with the first entry pre-filled
    explicit node(raw_value_type item) : pop_idx{0}, push_idx{step_size}, next{nullptr} {
      entries[0].value.store(item, std::memory_order_relaxed);
      for (unsigned i = 1; i < entries_per_node; i++) {
        entries[i].value.store(nullptr, std::memory_order_relaxed);
      }
    }

    ~node() override {
      for (unsigned i = pop_idx; i < push_idx; i += step_size) {
        traits::delete_value(entries[i % entries_per_node].value.load(std::memory_order_relaxed).get());
      }
    }
  };

  alignas(64) concurrent_ptr _head;
  alignas(64) concurrent_ptr _tail;
};

template <class T, class... Policies>
ramalhete_queue<T, Policies...>::ramalhete_queue() {
  auto n = new node(nullptr);
  n->push_idx.store(0, std::memory_order_relaxed);
  _head.store(n, std::memory_order_relaxed);
  _tail.store(n, std::memory_order_relaxed);
}

template <class T, class... Policies>
ramalhete_queue<T, Policies...>::~ramalhete_queue() {
  // (1) - this acquire-load synchronizes-with the release-CAS (13)
  auto n = _head.load(std::memory_order_acquire);
  while (n) {
    // (2) - this acquire-load synchronizes-with the release-CAS (4)
    auto next = n->next.load(std::memory_order_acquire);
    delete n.get();
    n = next;
  }
}

template <class T, class... Policies>
void ramalhete_queue<T, Policies...>::push(value_type value) {
  raw_value_type raw_val = traits::get_raw(value);
  if (raw_val == nullptr) {
    throw std::invalid_argument("value can not be nullptr");
  }

  backoff backoff;
  guard_ptr t;
  for (;;) {
    // (3) - this acquire-load synchronizes-with the release-CAS (5, 7)
    t.acquire(_tail, std::memory_order_acquire);

    unsigned idx = t->push_idx.fetch_add(step_size, std::memory_order_relaxed);
    if (idx >= max_idx) {
      // This node is full
      if (t != _tail.load(std::memory_order_relaxed)) {
        continue; // some other thread already added a new node.
      }

      auto next = t->next.load(std::memory_order_relaxed);
      if (next == nullptr) {
        node* new_node = new node(raw_val);
        traits::release(value);

        marked_ptr expected = nullptr;
        // (4) - this release-CAS synchronizes-with the acquire-load (2, 6, 12)
        if (t->next.compare_exchange_strong(expected, new_node, std::memory_order_release, std::memory_order_relaxed)) {
          expected = t;
          // (5) - this release-CAS synchronizes-with the acquire-load (3)
          _tail.compare_exchange_strong(expected, new_node, std::memory_order_release, std::memory_order_relaxed);
          return;
        }
        // prevent the pre-stored value from beeing deleted
        new_node->push_idx.store(0, std::memory_order_relaxed);
        // some other node already added a new node
        delete new_node;
      } else {
        // (6) - this acquire-load synchronizes-with the release-CAS (4)
        next = t->next.load(std::memory_order_acquire);
        marked_ptr expected = t;
        // (7) - this release-CAS synchronizes-with the acquire-load (3)
        _tail.compare_exchange_strong(expected, next, std::memory_order_release, std::memory_order_relaxed);
      }
      continue;
    }
    idx %= entries_per_node;

    marked_value expected = nullptr;
    // (8) - this release-CAS synchronizes-with the acquire-load (14) and the acquire-exchange (15)
    if (t->entries[idx].value.compare_exchange_strong(
          expected, raw_val, std::memory_order_release, std::memory_order_relaxed)) {
      traits::release(value);
      return;
    }

    backoff();
  }
}

template <class T, class... Policies>
bool ramalhete_queue<T, Policies...>::try_pop(value_type& result) {
  backoff backoff;

  guard_ptr h;
  for (;;) {
    // (9) - this acquire-load synchronizes-with the release-CAS (13)
    h.acquire(_head, std::memory_order_acquire);

    // (10) - this acquire-load synchronizes-with the release-fetch-add (11)
    const auto pop_idx = h->pop_idx.load(std::memory_order_acquire);
    // This synchronization is necessary to avoid a situation where we see an up-to-date
    // pop_idx, but an out-of-date push_idx and would (falsly) assume that the queue is empty.
    const auto push_idx = h->push_idx.load(std::memory_order_relaxed);
    if (pop_idx >= push_idx && h->next.load(std::memory_order_relaxed) == nullptr) {
      break;
    }

    // (11) - this release-fetch-add synchronizes with the acquire-load (10)
    unsigned idx = h->pop_idx.fetch_add(step_size, std::memory_order_release);
    if (idx >= max_idx) {
      // This node has been drained, check if there is another one
      // (12) - this acquire-load synchronizes-with the release-CAS (4)
      auto next = h->next.load(std::memory_order_acquire);
      if (next == nullptr) {
        break; // No more nodes in the queue
      }

      marked_ptr expected = h;
      // (13) - this release-CAS synchronizes-with the acquire-load (1, 9)
      if (_head.compare_exchange_strong(expected, next, std::memory_order_release, std::memory_order_relaxed)) {
        h.reclaim(); // The old node has been unlinked -> reclaim it.
      }

      continue;
    }
    idx %= entries_per_node;

    auto value = h->entries[idx].value.load(std::memory_order_relaxed);
    if constexpr (pop_retries > 0) {
      unsigned cnt = 0;
      ramalhete_queue::backoff retry_backoff;
      while (value == nullptr && ++cnt <= pop_retries) {
        value = h->entries[idx].value.load(std::memory_order_relaxed);
        retry_backoff(); // TODO - use a backoff type that can be configured separately
      }
    }

    if (value != nullptr) {
      // (14) - this acquire-load synchronizes-with the release-CAS (8)
      std::ignore = h->entries[idx].value.load(std::memory_order_acquire);
      traits::store(result, value.get());
      return true;
    }

    // (15) - this acquire-exchange synchronizes-with the release-CAS (8)
    value = h->entries[idx].value.exchange(marked_value(nullptr, 1), std::memory_order_acquire);
    if (value != nullptr) {
      traits::store(result, value.get());
      return true;
    }

    backoff();
  }

  return false;
}
} // namespace xenium

#ifdef _MSC_VER
  #pragma warning(pop)
#endif

#endif
