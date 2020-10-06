//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_DETAL_CONCURRENT_PTR_HPP
#define XENIUM_DETAL_CONCURRENT_PTR_HPP

#include <xenium/marked_ptr.hpp>

#include <atomic>

namespace xenium::reclamation::detail {

//! T must be derived from enable_concurrent_ptr<T>. D is a deleter.
template <class T, std::size_t N, template <class, class MarkedPtr> class GuardPtr>
class concurrent_ptr {
public:
  using marked_ptr = xenium::marked_ptr<T, N>;
  using guard_ptr = GuardPtr<T, marked_ptr>;

  concurrent_ptr(const marked_ptr& p = marked_ptr()) noexcept : _ptr(p) {} // NOLINT
  concurrent_ptr(const concurrent_ptr&) = delete;
  concurrent_ptr(concurrent_ptr&&) = delete;
  concurrent_ptr& operator=(const concurrent_ptr&) = delete;
  concurrent_ptr& operator=(concurrent_ptr&&) = delete;

  // Atomic load that does not guard target from being reclaimed.
  [[nodiscard]] marked_ptr load(std::memory_order order = std::memory_order_seq_cst) const { return _ptr.load(order); }

  // Atomic store.
  void store(const marked_ptr& src, std::memory_order order = std::memory_order_seq_cst) { _ptr.store(src, order); }

  // Shorthand for store (src.get())
  void store(const guard_ptr& src, std::memory_order order = std::memory_order_seq_cst) {
    _ptr.store(src.get(), order);
  }

  bool compare_exchange_weak(marked_ptr& expected,
                             marked_ptr desired,
                             std::memory_order order = std::memory_order_seq_cst) {
    return _ptr.compare_exchange_weak(expected, desired, order);
  }

  bool compare_exchange_weak(marked_ptr& expected,
                             marked_ptr desired,
                             std::memory_order order = std::memory_order_seq_cst) volatile {
    return _ptr.compare_exchange_weak(expected, desired, order);
  }

  bool compare_exchange_weak(marked_ptr& expected,
                             marked_ptr desired,
                             std::memory_order success,
                             std::memory_order failure) {
    return _ptr.compare_exchange_weak(expected, desired, success, failure);
  }

  bool compare_exchange_weak(marked_ptr& expected,
                             marked_ptr desired,
                             std::memory_order success,
                             std::memory_order failure) volatile {
    return _ptr.compare_exchange_weak(expected, desired, success, failure);
  }

  bool compare_exchange_strong(marked_ptr& expected,
                               marked_ptr desired,
                               std::memory_order order = std::memory_order_seq_cst) {
    return _ptr.compare_exchange_strong(expected, desired, order);
  }

  bool compare_exchange_strong(marked_ptr& expected,
                               marked_ptr desired,
                               std::memory_order order = std::memory_order_seq_cst) volatile {
    return _ptr.compare_exchange_strong(expected, desired, order);
  }

  bool compare_exchange_strong(marked_ptr& expected,
                               marked_ptr desired,
                               std::memory_order success,
                               std::memory_order failure) {
    return _ptr.compare_exchange_strong(expected, desired, success, failure);
  }

  bool compare_exchange_strong(marked_ptr& expected,
                               marked_ptr desired,
                               std::memory_order success,
                               std::memory_order failure) volatile {
    return _ptr.compare_exchange_strong(expected, desired, success, failure);
  }

private:
  std::atomic<marked_ptr> _ptr;
};
} // namespace xenium::reclamation::detail

#endif
