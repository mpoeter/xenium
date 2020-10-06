//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_VYUKOV_BOUNDED_QUEUE_HPP
#define XENIUM_VYUKOV_BOUNDED_QUEUE_HPP

#include <xenium/parameter.hpp>
#include <xenium/utils.hpp>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif

namespace xenium {

namespace policy {
  /**
   * @brief Policy to configure whether `try_push`/`try_pop` in `vyukov_bounded_queue`
   * should default to `try_push_weak`/`try_pop_weak`.
   * @tparam Value
   */
  template <bool Value>
  struct default_to_weak;

} // namespace policy
/**
 * @brief A bounded generic multi-producer/multi-consumer FIFO queue.
 *
 * This implementation is based on the bounded MPMC queue proposed by Vyukov
 * \[[Vy10](index.html#ref-vyukov-2010)\].
 * It is fully generic and can handle any type `T`, as long as it is default
 * constructible and either copyable or movable.
 *
 * Producers and consumers operate independently, but the operations are not
 * lock-free, i.e., a `try_pop` may have to wait for a pending `try_push`
 * operation to finish and vice versa.
 *
 * The `try_push_weak` and `try_pop_weak` versions are lock-free, but may fail
 * in situations were the non-weak versions would succeed. Consider the following
 * situation: Thread T1 and thread T2 both push to the queue. T1 starts before T2,
 * but T2 finishes faster. Now T2 tries to pop from the queue. The queue is not empty
 * (T2 has previously pushed an element, and there are no other pop operations involved),
 * but T1 has still not finished its push operation. However, because T1 has started its
 * push before T2, this is the next item to be popped. In such a situation `try_pop`
 * would keep spinning until T1 has finished, while `try_pop_weak` would immediately
 * return false, even though the queue is not empty.
 *
 * @tparam T type of the stored elements.
 */
template <class T, class... Policies>
struct vyukov_bounded_queue {
public:
  using value_type = T;

  static constexpr bool default_to_weak =
    parameter::value_param_t<bool, policy::default_to_weak, false, Policies...>::value;
  ;

  /**
   * @brief Constructs a new instance with the specified maximum size.
   * @param size max number of elements in the queue; must be a power of two greater one.
   */
  explicit vyukov_bounded_queue(std::size_t size) : cells(new cell[size]), index_mask(size - 1) {
    assert(size >= 2 && utils::is_power_of_two(size));
    for (std::size_t i = 0; i < size; ++i) {
      cells[i].sequence.store(i, std::memory_order_relaxed);
    }
    enqueue_pos.store(0, std::memory_order_relaxed);
    dequeue_pos.store(0, std::memory_order_relaxed);
  }

  vyukov_bounded_queue(const vyukov_bounded_queue&) = delete;
  vyukov_bounded_queue(vyukov_bounded_queue&&) = delete;

  vyukov_bounded_queue& operator=(const vyukov_bounded_queue&) = delete;
  vyukov_bounded_queue& operator=(vyukov_bounded_queue&&) = delete;

  /**
   * @brief Tries to push a new element to the queue.
   *
   * If `policy::default_to_weak` has been specified to be true, this method
   * forwards to `try_push_weak`, otherwise it forwards to `try_push_strong`.
   *
   * Progress guarantees: see `try_push_weak`/`try_push_strong`
   *
   * @tparam Args
   * @param args
   * @return `true` if the operation was successful, otherwise `false`
   */
  template <class... Args>
  bool try_push(Args&&... args) {
    return do_try_push<default_to_weak>(std::forward<Args>(args)...);
  }

  /**
   * @brief Tries to push a new element to the queue.
   *
   * Tries to reserve a cell for the new element as long as the number of elements does
   * not exceed the specified size.
   *
   * If the argument is of type `T`, the element is either copy-assigned or move-assigned
   * (depending on whether `Args` is an l-value or r-value). Otherwise, a temporary `T`
   * instance is created using perfect-forwarding on the given arguments, which is then
   * move-assigned.
   *
   * Progress guarantees: blocking
   *
   * @tparam Args
   * @param args
   * @return `true` if the operation was successful, otherwise `false`
   */
  template <class... Args>
  bool try_push_strong(Args&&... args) {
    return do_try_push<false>(std::forward<Args>(args)...);
  }

  /**
   * @brief Tries to push a new element to the queue.
   *
   * Tries to reserve a cell for the new element; fails if the queue is full or
   * a pop operation on the element to be pushed to is still pending.
   *
   * If the argument is of type `T`, the element is either copy-assigned or move-assigned
   * (depending on whether `Args` is an l-value or r-value). Otherwise, a temporary `T`
   * instance is created using perfect-forwarding on the given arguments, which is then
   * move-assigned.
   *
   * Progress guarantees: lock-free
   *
   * @tparam Args
   * @param args
   * @return `true` if the operation was successful, otherwise `false`
   */
  template <class... Args>
  bool try_push_weak(Args&&... args) {
    return do_try_push<true>(std::forward<Args>(args)...);
  }

  /**
   * @brief Tries to pop an element from the queue.
   *
   * If `policy::default_to_weak` has been specified to be true, this method
   * forwards to `try_pop_weak`, otherwise it forwards to `try_pop_strong`.
   *
   * Progress guarantees: see `try_pop_weak`/`try_pop_strong`
   *
   * @param result the value popped from the queue if the operation was successful
   * @return `true` if the operation was successful, otherwise `false`
   */
  [[nodiscard]] bool try_pop(T& result) { return do_try_pop<default_to_weak>(result); }

  /**
   * @brief Tries to pop an element from the queue as long as the queue is not empty.
   *
   * In case a push operation is still pending `try_pop` keeps spinning until the
   * push has finished.
   *
   * Progress guarantees: blocking
   *
   * @param result the value popped from the queue if the operation was successful
   * @return `true` if the operation was successful, otherwise `false`
   */
  [[nodiscard]] bool try_pop_strong(T& result) { return do_try_pop<false>(result); }

  /**
   * @brief Tries to pop an element from the queue.
   *
   * This operation fails if the queue is empty or a push operation on the element
   * to be popped is still pending.
   *
   * Progress guarantees: lock-free
   *
   * @param result the value popped from the queue if the operation was successful
   * @return `true` if the operation was successful, otherwise `false`
   */
  [[nodiscard]] bool try_pop_weak(T& result) { return do_try_pop<true>(result); }

private:
  template <bool Weak, class... Args>
  bool do_try_push(Args&&... args) {
    cell* c;
    std::size_t pos = enqueue_pos.load(std::memory_order_relaxed);
    for (;;) {
      c = &cells[pos & index_mask];
      // (3) - this acquire-load synchronizes-with the release-store (2)
      std::size_t seq = c->sequence.load(std::memory_order_acquire);
      if (seq == pos) {
        if (enqueue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
          break;
        }
      } else {
        if (Weak) {
          if (seq < pos) {
            return false;
          }
          pos = enqueue_pos.load(std::memory_order_relaxed);
        } else {
          auto pos2 = enqueue_pos.load(std::memory_order_relaxed);
          if (pos2 == pos && dequeue_pos.load(std::memory_order_relaxed) + index_mask + 1 == pos) {
            return false;
          }
          pos = pos2;
        }
      }
    }
    assign_value(c->value, std::forward<Args>(args)...);
    // (4) - this release-store synchronizes-with the acquire-load (1)
    c->sequence.store(pos + 1, std::memory_order_release);
    return true;
  }

  template <bool Weak>
  bool do_try_pop(T& result) {
    cell* c;
    std::size_t pos = dequeue_pos.load(std::memory_order_relaxed);
    for (;;) {
      c = &cells[pos & index_mask];
      // (1) - this acquire-load synchronizes-with the release-store (4)
      std::size_t seq = c->sequence.load(std::memory_order_acquire);
      auto new_pos = pos + 1;
      if (seq == new_pos) {
        if (dequeue_pos.compare_exchange_weak(pos, new_pos, std::memory_order_relaxed)) {
          break;
        }
      } else {
        if (Weak) {
          if (seq < new_pos) {
            return false;
          }
          pos = dequeue_pos.load(std::memory_order_relaxed);
        } else {
          auto pos2 = dequeue_pos.load(std::memory_order_relaxed);
          if (pos2 == pos && enqueue_pos.load(std::memory_order_relaxed) == pos) {
            return false;
          }
          pos = pos2;
        }
      }
    }
    result = std::move(c->value);
    // (2) - this release-store synchronizes-with the acquire-load (3)
    c->sequence.store(pos + index_mask + 1, std::memory_order_release);
    return true;
  }

  void assign_value(T& v, const T& source) { v = source; }
  void assign_value(T& v, T&& source) { v = std::move(source); }
  template <class... Args>
  void assign_value(T& v, Args&&... args) {
    v = T{std::forward<Args>(args)...};
  }

  // TODO - add optional padding via policy
  struct cell {
    std::atomic<std::size_t> sequence;
    T value;
  };

  std::unique_ptr<cell[]> cells;
  const std::size_t index_mask;
  alignas(64) std::atomic<size_t> enqueue_pos;
  alignas(64) std::atomic<size_t> dequeue_pos;
};
} // namespace xenium

#ifdef _MSC_VER
  #pragma warning(pop)
#endif

#endif
