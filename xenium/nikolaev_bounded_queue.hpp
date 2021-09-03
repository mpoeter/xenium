//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_NIKOLAEV_BOUNDED_QUEUE_HPP
#define XENIUM_NIKOLAEV_BOUNDED_QUEUE_HPP

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
  explicit nikolaev_bounded_queue(std::size_t capacity);
  ~nikolaev_bounded_queue();

  nikolaev_bounded_queue(const nikolaev_bounded_queue&) = delete;
  nikolaev_bounded_queue(nikolaev_bounded_queue&&) = delete;

  nikolaev_bounded_queue& operator=(const nikolaev_bounded_queue&) = delete;
  nikolaev_bounded_queue& operator=(nikolaev_bounded_queue&&) = delete;

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
  [[nodiscard]] std::size_t capacity() const noexcept { return _capacity; }

private:
  using storage_t = typename std::aligned_storage<sizeof(T), alignof(T)>::type;
  const std::size_t _capacity;
  const std::size_t _remap_shift;
  std::unique_ptr<storage_t[]> _storage;
  detail::nikolaev_scq _allocated_queue;
  detail::nikolaev_scq _free_queue;
};

template <class T, class... Policies>
nikolaev_bounded_queue<T, Policies...>::nikolaev_bounded_queue(std::size_t capacity) :
    _capacity(utils::next_power_of_two(capacity)),
    _remap_shift(detail::nikolaev_scq::calc_remap_shift(_capacity)),
    _storage(new storage_t[_capacity]),
    _allocated_queue(_capacity, _remap_shift, detail::nikolaev_scq::empty_tag{}),
    _free_queue(_capacity, _remap_shift, detail::nikolaev_scq::full_tag{}) {
  assert(capacity > 0);
}

template <class T, class... Policies>
nikolaev_bounded_queue<T, Policies...>::~nikolaev_bounded_queue() {
  std::uint64_t eidx;
  while (_allocated_queue.dequeue<false, pop_retries>(eidx, _capacity, _remap_shift)) {
    reinterpret_cast<T&>(_storage[eidx]).~T();
  }
}

template <class T, class... Policies>
bool nikolaev_bounded_queue<T, Policies...>::try_push(value_type value) {
  std::uint64_t eidx;
  // TODO - make nonempty checks configurable
  if (!_free_queue.dequeue<false, pop_retries>(eidx, _capacity, _remap_shift)) {
    return false;
  }

  assert(eidx < _capacity);
  new (&_storage[eidx]) T(std::move(value));
  _allocated_queue.enqueue<false, false>(eidx, _capacity, _remap_shift);
  return true;
}

template <class T, class... Policies>
bool nikolaev_bounded_queue<T, Policies...>::try_pop(value_type& result) {
  std::uint64_t eidx;
  // TODO - make nonempty checks configurable
  if (!_allocated_queue.dequeue<false, pop_retries>(eidx, _capacity, _remap_shift)) {
    return false;
  }

  assert(eidx < _capacity);
  T& data = reinterpret_cast<T&>(_storage[eidx]);
  result = std::move(data);
  data.~T(); // NOLINT (use-after-move)
  _free_queue.enqueue<false, false>(eidx, _capacity, _remap_shift);
  return true;
}
} // namespace xenium

#endif