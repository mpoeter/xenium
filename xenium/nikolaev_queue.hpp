//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_NIKOLAEV_QUEUE_HPP
#define XENIUM_NIKOLAEV_QUEUE_HPP

#include <xenium/parameter.hpp>
#include <xenium/policy.hpp>
#include <xenium/utils.hpp>

#include <xenium/detail/nikolaev_scq.hpp>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>

namespace xenium {
/**
 * @brief An unbounded lock-free multi-producer/multi-consumer queue.
 *
 * This implementation is based on the unbounded MPMC queue proposed by Nikolaev
 * \[[Nik19](index.html#ref-nikolaev-2019)\].
 *
 * The nikoleav_queue provides lock-free progress guarantee under the condition that the
 * number of threads concurrently operating on the queue is less than the capacity of a node
 * (see `entries_per_node` policy).
 *
 * Supported policies:
 *  * `xenium::policy::reclaimer`<br>
 *    Defines the reclamation scheme to be used for internal nodes. (**required**)
 *  * `xenium::policy::entries_per_node`<br>
 *    Defines the number of entries for each internal node. This must be a power of two.
 *    (*optional*; defaults to 512)
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
class nikolaev_queue {
public:
  using value_type = T;
  using reclaimer = parameter::type_param_t<policy::reclaimer, parameter::nil, Policies...>;
  static constexpr unsigned pop_retries =
    parameter::value_param_t<unsigned, policy::pop_retries, 1000, Policies...>::value;
  static constexpr unsigned entries_per_node =
    parameter::value_param_t<unsigned, policy::entries_per_node, 512, Policies...>::value;

  static_assert(utils::is_power_of_two(entries_per_node), "entries_per_node must be a power of two");
  static_assert(parameter::is_set<reclaimer>::value, "reclaimer policy must be specified");

  template <class... NewPolicies>
  using with = nikolaev_queue<T, NewPolicies..., Policies...>;

  nikolaev_queue();
  ~nikolaev_queue();

  nikolaev_queue(const nikolaev_queue&) = delete;
  nikolaev_queue(nikolaev_queue&&) = delete;

  nikolaev_queue& operator=(const nikolaev_queue&) = delete;
  nikolaev_queue& operator=(nikolaev_queue&&) = delete;

  /**
   * @brief Pushes the given value.
   *
   * Progress guarantees: lock-free
   *
   * @param value
   */
  void push(value_type value);

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
  struct node;

  using concurrent_ptr = typename reclaimer::template concurrent_ptr<node, 0>;
  using marked_ptr = typename concurrent_ptr::marked_ptr;
  using guard_ptr = typename concurrent_ptr::guard_ptr;

  using storage_t = typename std::aligned_storage<sizeof(T), alignof(T)>::type;

  static constexpr unsigned remap_shift = detail::nikolaev_scq::calc_remap_shift(entries_per_node);

  // TODO - preallocate memory for storage and queues together with node
  struct node : reclaimer::template enable_concurrent_ptr<node> {
    node() :
        _storage(new storage_t[entries_per_node]),
        _allocated_queue(entries_per_node, remap_shift, detail::nikolaev_scq::empty_tag{}),
        _free_queue(entries_per_node, remap_shift, detail::nikolaev_scq::full_tag{}) {}

    explicit node(value_type&& value) :
        _storage(new storage_t[entries_per_node]),
        _allocated_queue(entries_per_node, remap_shift, detail::nikolaev_scq::first_used_tag{}),
        _free_queue(entries_per_node, remap_shift, detail::nikolaev_scq::first_empty_tag{}) {
      new (&_storage[0]) T(std::move(value));
    }

    ~node() override {
      std::uint64_t eidx;
      while (_allocated_queue.dequeue<false, pop_retries>(eidx, entries_per_node, remap_shift)) {
        reinterpret_cast<T&>(_storage[eidx]).~T();
      }
    }

    void steal_init_value(value_type& value) {
      bool success = try_pop(value);
      (void)success;
      assert(success);
    }

    bool try_push(value_type&& value) {
      std::uint64_t eidx;
      if (!_free_queue.dequeue<false, pop_retries>(eidx, entries_per_node, remap_shift)) {
        _allocated_queue.finalize();
        return false;
      }

      assert(eidx < entries_per_node);
      new (&_storage[eidx]) T(std::move(value));
      if (!_allocated_queue.enqueue<false, true>(eidx, entries_per_node, remap_shift)) {
        // queue has been finalized
        // we have already moved the value, so we need to move it back and
        // destroy the created storage item.
        T& data = reinterpret_cast<T&>(_storage[eidx]);
        value = std::move(data);
        data.~T(); // NOLINT (use-after-move)
        _free_queue.enqueue<false, false>(eidx, entries_per_node, remap_shift);
        return false;
      }
      return true;
    }

    bool try_pop(value_type& result) {
      std::uint64_t eidx;
      if (!_allocated_queue.dequeue<false, pop_retries>(eidx, entries_per_node, remap_shift)) {
        return false;
      }

      assert(eidx < entries_per_node);
      T& data = reinterpret_cast<T&>(_storage[eidx]);
      result = std::move(data);
      data.~T(); // NOLINT (use-after-move)
      _free_queue.enqueue<false, false>(eidx, entries_per_node, remap_shift);
      return true;
    }

    std::unique_ptr<storage_t[]> _storage;
    detail::nikolaev_scq _allocated_queue;
    detail::nikolaev_scq _free_queue;

    concurrent_ptr _next;
  };

  concurrent_ptr _tail;
  concurrent_ptr _head;
};

template <class T, class... Policies>
nikolaev_queue<T, Policies...>::nikolaev_queue() {
  auto n = new node();
  _tail.store(n, std::memory_order_relaxed);
  _head.store(n, std::memory_order_relaxed);
}

template <class T, class... Policies>
nikolaev_queue<T, Policies...>::~nikolaev_queue() {
  auto h = _head.load(std::memory_order_relaxed).get();
  while (h != nullptr) {
    auto next = h->_next.load(std::memory_order_relaxed).get();
    delete h;
    h = next;
  }
}

template <class T, class... Policies>
void nikolaev_queue<T, Policies...>::push(value_type value) {
  guard_ptr n;
  for (;;) {
    // (1) - this acquire-load synchronizes-with the release-CAS (3, 5)
    n.acquire(_tail, std::memory_order_acquire);
    if (n->_next.load(std::memory_order_relaxed) != nullptr) {
      // (2) - this acquire-load synchronizes-with the release-CAS (4)
      const auto next = n->_next.load(std::memory_order_acquire);
      marked_ptr expected = n;
      // (3) - this release-CAS synchronizes with the acquire-load (1)
      _tail.compare_exchange_weak(expected, next, std::memory_order_release, std::memory_order_relaxed);
      continue;
    }

    if (n->try_push(std::move(value))) {
      return;
    }

    auto next = new node(std::move(value)); // NOLINT (use-after-move)
    marked_ptr expected{nullptr};
    // (4) - this release-CAS synchronizes-with the acquire-load (2, 7)
    if (n->_next.compare_exchange_strong(expected, next, std::memory_order_release, std::memory_order_relaxed)) {
      expected = n;
      // (5) - this release-CAS synchronizes-with the acquire-load (1)
      _tail.compare_exchange_strong(expected, next, std::memory_order_release, std::memory_order_relaxed);
      return;
    }
    next->steal_init_value(value);
    delete next;
  }
}

template <class T, class... Policies>
bool nikolaev_queue<T, Policies...>::try_pop(value_type& result) {
  guard_ptr n;
  for (;;) {
    // (6) - this acquire-load synchronizes-with the release-CAS (8)
    n.acquire(_head, std::memory_order_acquire);
    if (n->try_pop(result)) {
      return true;
    }
    if (n->_next.load(std::memory_order_relaxed) == nullptr) {
      return false;
    }

    n->_allocated_queue.set_threshold(3 * entries_per_node - 1);
    if (n->try_pop(result)) {
      return true;
    }

    // (7) - this acquire-load synchronizes-with (4)
    const auto next = n->_next.load(std::memory_order_acquire);
    marked_ptr expected = n;
    // (8) - this release-CAS synchronizes-with the acquire-load (6)
    if (_head.compare_exchange_weak(expected, next, std::memory_order_release, std::memory_order_relaxed)) {
      n.reclaim();
    }
  }
}
} // namespace xenium

#endif