//
// Copyright (c) 2018 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_CHASE_WORK_STEALING_DEQUE_HPP
#define XENIUM_CHASE_WORK_STEALING_DEQUE_HPP

#include <xenium/parameter.hpp>
#include <xenium/policy.hpp>
#include <xenium/detail/fixed_size_circular_array.hpp>

#include <atomic>
#include <cassert>

namespace xenium {

template <class T, class... Policies>
struct chase_work_stealing_deque {
  using value_type = T*;
  static constexpr unsigned capacity = parameter::value_param_t<unsigned, policy::capacity, 128, Policies...>::value;

  chase_work_stealing_deque();

  chase_work_stealing_deque(const chase_work_stealing_deque&) = delete;
  chase_work_stealing_deque(chase_work_stealing_deque&&) = delete;

  chase_work_stealing_deque& operator=(const chase_work_stealing_deque&) = delete;
  chase_work_stealing_deque& operator=(chase_work_stealing_deque&&) = delete;

  bool try_push(value_type item);
  bool try_pop(value_type &result);
  bool try_steal(value_type &result);

  unsigned size() {
    auto t = top.load(std::memory_order_relaxed);
    return bottom.load(std::memory_order_relaxed) - t; }
private:
  detail::fixed_size_circular_array<T, capacity> items;
  std::atomic<unsigned> bottom;
  std::atomic<unsigned> top;
};

template <class T, class... Policies>
chase_work_stealing_deque<T, Policies...>::chase_work_stealing_deque() :
  bottom(),
  top()
{}

template <class T, class... Policies>
bool chase_work_stealing_deque<T, Policies...>::try_push(value_type item) {
  auto b = bottom.load(std::memory_order_relaxed);
  auto t = top.load(std::memory_order_relaxed);
  auto size = b - t;
  if (size >= items.capacity()) {
    // TODO - grow if array is growable
    return false;
  }

  items.put(b, item, std::memory_order_relaxed);

  // (TODO)
  bottom.store(b + 1, std::memory_order_release);
  return true;
}

template <class T, class... Policies>
bool chase_work_stealing_deque<T, Policies...>::try_pop(value_type &result) {
  auto b = bottom.load(std::memory_order_relaxed);
  auto t = top.load(std::memory_order_relaxed);
  if (b == t)
    return false;

  --b;
  // (TODO)
  bottom.store(b, std::memory_order_seq_cst);

  auto item = items.get(b, std::memory_order_relaxed);
  t = top.load(std::memory_order_acquire);
  if (b > t) {
    result = item;
    return true;
  }

  if (b == t) {
    // (TODO)
    if (top.compare_exchange_strong(t, t + 1, std::memory_order_release, std::memory_order_relaxed)) {
      bottom.store(t + 1, std::memory_order_relaxed);
      result = item;
      return true;
    } else {
      bottom.store(t, std::memory_order_relaxed);
      return false;
    }
  }

  assert(b == t - 1);
  bottom.store(t, std::memory_order_relaxed);
  return false;
}

template <class T, class... Policies>
bool chase_work_stealing_deque<T, Policies...>::try_steal(value_type &result) {
  // (TODO)
  auto t = top.load(std::memory_order_acquire);
  auto b = bottom.load(std::memory_order_acquire);
  auto size = (int)b - (int)t;
  if (size <= 0)
    return false;

  auto item = items.get(t, std::memory_order_relaxed);
  // (TODO)
  if (top.compare_exchange_strong(t, t + 1, std::memory_order_acquire, std::memory_order_relaxed)) {
    result = item;
    return true;
  }

  return false;
}
}

#endif
