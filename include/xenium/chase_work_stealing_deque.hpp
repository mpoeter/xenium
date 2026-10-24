//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_CHASE_WORK_STEALING_DEQUE_HPP
#define XENIUM_CHASE_WORK_STEALING_DEQUE_HPP

#include <xenium/detail/fixed_size_circular_array.hpp>
#include <xenium/detail/growing_circular_array.hpp>
#include <xenium/parameter.hpp>
#include <xenium/policy.hpp>

#include <atomic>
#include <cassert>

namespace xenium {
/**
 * @brief A lock-free work stealing deque.
 *
 * This is an implementation of the work stealing deque proposed by Chase and Lev
 * \[[CL05](index.html#ref-chase-2005)\].
 *
 * Supported policies:
 *  * `xenium::policy::capacity`<br>
 *    Defines the (minimum) capacity of the deque. (*optional*; defaults to 128)
 *  * `xenium::policy::container`<br>
 *    Defines the internal container type to store the entries.
 *    (*optional*; defaults to `xenium::detail::growing_circular_array`)<br>
 *    Possible containers are:
 *    * `xenium::detail::fixed_size_circular_array`
 *    * `xenium::detail::growing_circular_array`
 *
 * @tparam T
 * @tparam Policies
 */
template <class T, class... Policies>
struct chase_work_stealing_deque {
  using value_type = T*;
  static constexpr std::size_t capacity =
    parameter::value_param_t<std::size_t, policy::capacity, 128, Policies...>::value;
  using container =
    parameter::type_param_t<policy::container, detail::growing_circular_array<T, capacity>, Policies...>;

  chase_work_stealing_deque();

  chase_work_stealing_deque(const chase_work_stealing_deque&) = delete;
  chase_work_stealing_deque(chase_work_stealing_deque&&) = delete;

  chase_work_stealing_deque& operator=(const chase_work_stealing_deque&) = delete;
  chase_work_stealing_deque& operator=(chase_work_stealing_deque&&) = delete;

  bool try_push(value_type item);
  [[nodiscard]] bool try_pop(value_type& result);
  [[nodiscard]] bool try_steal(value_type& result);

  std::size_t size() {
    auto t = _top.load(std::memory_order_relaxed);
    return _bottom.load(std::memory_order_relaxed) - t;
  }

private:
  container _items;
  std::atomic<std::size_t> _bottom;
  std::atomic<std::size_t> _top;
};

template <class T, class... Policies>
chase_work_stealing_deque<T, Policies...>::chase_work_stealing_deque() : _bottom(), _top() {}

template <class T, class... Policies>
bool chase_work_stealing_deque<T, Policies...>::try_push(value_type item) {
  auto b = _bottom.load(std::memory_order_relaxed);
  auto t = _top.load(std::memory_order_relaxed);
  auto size = b - t;
  if (size >= _items.capacity()) {
    if (_items.can_grow()) {
      _items.grow(b, t);
      assert(size < _items.capacity());
      // TODO - need to update _top??
    } else {
      return false;
    }
  }

  _items.put(b, item, std::memory_order_relaxed);

  // (1) - this release-store synchronizes-with the seq-cst-load (4)
  _bottom.store(b + 1, std::memory_order_release);
  return true;
}

template <class T, class... Policies>
bool chase_work_stealing_deque<T, Policies...>::try_pop(value_type& result) {
  auto b = _bottom.load(std::memory_order_relaxed);
  auto t = _top.load(std::memory_order_relaxed);
  if (b == t) {
    return false;
  }

  // We have to use seq-cst order for operations on _bottom as well as _top to ensure
  // that when two threads compete for the last item either one sees the updated _bottom
  // (pop wins), or one sees the updated _top (steal wins).

  --b;
  // (2) - this seq-cst-store enforces a total order with the seq-cst-load (4)
  _bottom.store(b, std::memory_order_seq_cst);

  auto* item = _items.get(b, std::memory_order_relaxed);
  // (3) - this seq-cst-load enforces a total order with the seq-cst-CAS (5)
  t = _top.load(std::memory_order_seq_cst);
  if (b > t) {
    result = item;
    return true;
  }

  if (b == t) {
    if (_top.compare_exchange_strong(t, t + 1, std::memory_order_relaxed)) {
      _bottom.store(t + 1, std::memory_order_relaxed);
      result = item;
      return true;
    }
    _bottom.store(t, std::memory_order_relaxed);
    return false;
  }

  assert(b == t - 1);
  _bottom.store(t, std::memory_order_relaxed);
  return false;
}

template <class T, class... Policies>
bool chase_work_stealing_deque<T, Policies...>::try_steal(value_type& result) {
  auto t = _top.load(std::memory_order_relaxed);

  // (4) - this seq-cst-load enforces a total order with the seq-cst-store (2)
  //       and synchronizes-with the release-store (1)
  auto b = _bottom.load(std::memory_order_seq_cst);
  auto size = static_cast<std::intptr_t>(b) - static_cast<std::intptr_t>(t);
  if (size <= 0) {
    return false;
  }

  auto* item = _items.get(t, std::memory_order_relaxed);
  // (5) - this seq-cst-CAS enforces a total order with the seq-cst-load (3)
  if (_top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
    result = item;
    return true;
  }

  return false;
}
} // namespace xenium

#endif
