//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_MICHAEL_SCOTT_QUEUE_HPP
#define XENIUM_MICHAEL_SCOTT_QUEUE_HPP

#include <xenium/acquire_guard.hpp>
#include <xenium/backoff.hpp>
#include <xenium/marked_ptr.hpp>
#include <xenium/parameter.hpp>
#include <xenium/policy.hpp>

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif

namespace xenium {
/**
 * @brief An unbounded generic lock-free multi-producer/multi-consumer FIFO queue.
 *
 * This is an implementation of the lock-free MPMC queue proposed by Michael and Scott
 * \[[MS96](index.html#ref-michael-1996)\].
 * It is fully generic and can handle any type `T` that is copyable or movable.
 *
 * Supported policies:
 *  * `xenium::policy::reclaimer`<br>
 *    Defines the reclamation scheme to be used for internal nodes. (**required**)
 *  * `xenium::policy::backoff`<br>
 *    Defines the backoff strategy. (*optional*; defaults to `xenium::no_backoff`)
 *
 * @tparam T type of the stored elements.
 * @tparam Policies list of policies to customize the behaviour
 */
template <class T, class... Policies>
class michael_scott_queue {
public:
  using value_type = T;
  using reclaimer = parameter::type_param_t<policy::reclaimer, parameter::nil, Policies...>;
  using backoff = parameter::type_param_t<policy::backoff, no_backoff, Policies...>;

  template <class... NewPolicies>
  using with = michael_scott_queue<T, NewPolicies..., Policies...>;

  static_assert(parameter::is_set<reclaimer>::value, "reclaimer policy must be specified");

  michael_scott_queue();
  ~michael_scott_queue();

  /**
   * @brief Pushes the given value to the queue.
   *
   * This operation always allocates a new node.
   * Progress guarantees: lock-free (always performs a memory allocation)
   *
   * @param value
   */
  void push(T value);

  /**
   * @brief Tries to pop an object from the queue. If the operation is
   * successful, the object will be moved to `result`.
   *
   * Progress guarantees: lock-free
   *
   * @param result
   * @return `true` if the operation was successful, otherwise `false`
   */
  [[nodiscard]] bool try_pop(T& result);

private:
  struct node;

  using concurrent_ptr = typename reclaimer::template concurrent_ptr<node, 0>;
  using marked_ptr = typename concurrent_ptr::marked_ptr;
  using guard_ptr = typename concurrent_ptr::guard_ptr;

  struct node : reclaimer::template enable_concurrent_ptr<node> {
    node() : _value(){};
    explicit node(T&& v) : _value(std::move(v)) {}

    T _value;
    concurrent_ptr _next;
  };

  alignas(64) concurrent_ptr _head;
  alignas(64) concurrent_ptr _tail;
};

template <class T, class... Policies>
michael_scott_queue<T, Policies...>::michael_scott_queue() {
  auto n = new node();
  _head.store(n, std::memory_order_relaxed);
  _tail.store(n, std::memory_order_relaxed);
}

template <class T, class... Policies>
michael_scott_queue<T, Policies...>::~michael_scott_queue() {
  // (1) - this acquire-load synchronizes-with the release-CAS (11)
  auto n = _head.load(std::memory_order_acquire);
  while (n) {
    // (2) - this acquire-load synchronizes-with the release-CAS (6)
    auto next = n->_next.load(std::memory_order_acquire);
    delete n.get();
    n = next;
  }
}

template <class T, class... Policies>
void michael_scott_queue<T, Policies...>::push(T value) {
  node* n = new node(std::move(value));

  backoff backoff;

  guard_ptr t;
  for (;;) {
    // Get the old _tail pointer.
    // (3) - this acquire-load synchronizes-with the release-CAS (5, 7, 10)
    t.acquire(_tail, std::memory_order_acquire);

    // Help update the _tail pointer if needed.
    // (4) - this acquire-load synchronizes-with the release-CAS (6)
    auto next = t->_next.load(std::memory_order_acquire);
    if (next.get() != nullptr) {
      marked_ptr expected(t.get());
      // (5) - this release-CAS synchronizes-with the acquire-load (3)
      _tail.compare_exchange_weak(expected, next, std::memory_order_release, std::memory_order_relaxed);
      continue;
    }

    // Attempt to link in the new element.
    marked_ptr null{};
    // (6) - this release-CAS synchronizes-with the acquire-load (2, 4, 9).
    if (t->_next.compare_exchange_weak(null, n, std::memory_order_release, std::memory_order_relaxed)) {
      break;
    }

    backoff();
  }

  // Swing the tail to the new element.
  marked_ptr expected = t.get();
  // (7) - this release-CAS synchronizes-with the acquire-load (3)
  _tail.compare_exchange_strong(expected, n, std::memory_order_release, std::memory_order_relaxed);
}

template <class T, class... Policies>
bool michael_scott_queue<T, Policies...>::try_pop(T& result) {
  backoff backoff;

  guard_ptr h;
  for (;;) {
    // Get the old _head and _tail elements.
    // (8) - this acquire-load synchronizes-with the release-CAS (11)
    h.acquire(_head, std::memory_order_acquire);

    // Get the _head element's successor.
    // (9) - this acquire-load synchronizes-with the release-CAS (6).
    auto next = acquire_guard(h->_next, std::memory_order_acquire);
    if (_head.load(std::memory_order_relaxed).get() != h.get()) {
      continue;
    }

    // If the _head (dummy) element is the only one, return false to signal that
    // the operation has failed (no element has been returned).
    if (next.get() == nullptr) {
      return false;
    }

    marked_ptr t = _tail.load(std::memory_order_relaxed);

    // There are multiple elements. Help update _tail if needed.
    if (h.get() == t.get()) {
      // (10) - this release-CAS synchronizes-with the acquire-load (3)
      _tail.compare_exchange_weak(t, next, std::memory_order_release, std::memory_order_relaxed);
      continue;
    }

    // Attempt to update the _head pointer so that it points to the new dummy node.
    marked_ptr expected(h.get());
    // (11) - this release-CAS synchronizes-with the acquire-load (1, 8)
    if (_head.compare_exchange_weak(expected, next, std::memory_order_release, std::memory_order_relaxed)) {
      // return the data of _head's successor; it is the new dummy node.
      result = std::move(next->_value);
      break;
    }

    backoff();
  }

  // The old dummy node has been unlinked, so reclaim it.
  h.reclaim();

  return true;
}
} // namespace xenium

#ifdef _MSC_VER
  #pragma warning(pop)
#endif

#endif
