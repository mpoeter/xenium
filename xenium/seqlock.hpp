//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_SEQLOCK
#define XENIUM_SEQLOCK

#include <xenium/detail/port.hpp>
#include <xenium/parameter.hpp>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>

namespace xenium {

namespace policy {
  /**
   * @brief Policy to configure the number of slots used in `seqlock`.
   * @tparam Value
   */
  template <unsigned Value>
  struct slots;
} // namespace policy

/**
 * @brief An implementation of the sequence lock (also often referred to as "sequential lock").
 *
 * A `seqlock` holds an instance of T and allows efficient concurrent reads, even in case of concurrent
 * writes. In contrast to classic read-write-locks, readers to not have to have to announce the read
 * operation and can therefore not block a write operation. Instead, a read operation that overlaps
 * with a write operation will have to be retried in order to obtain a consistent snapshot.
 * However, this imposes additional limitations on the type T, which must be default constructible,
 * trivially copyable and trivially destructible.
 *
 * *Note:* T should not contain pointers that may get deleted due to an update of the stored
 * instance. The `seqlock` can only provide a consistent snapshot of the stored T instance, but
 * does not provide any guarantees about satellite data that T might refer to.
 *
 * The current implementation uses an implicit spin lock on the sequence counter to synchronize
 * write operations. In the future this will also be customizable.
 *
 * The current implementation is not strictly conformant with the current C++ standard, simply because
 * at the moment it is not possible to do this in a standard conform way. However, this implementation
 * should still work on all architectures. The situation will hopefully improve with C++20
 * (see http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1478r0.html for more details).
 *
 * Supported policies:
 *  * `xenium::policy::slots`<br>
 *    Defines the number of internal value slots, where each slot can hold a T instance.
 *    A number of slots >1 increases memory requirements, but makes the `load` operation
 *    lock-free and can reduce the number of retries due to concurrent updates.
 *    (optional; defaults to 1)
 *
 * @tparam T type of the stored element; T must be default constructible, trivially copyable
 *   and trivially destructible.
 */
template <class T, class... Policies>
struct seqlock {
  using value_type = T;

  static_assert(std::is_default_constructible_v<T>, "T must be default constructible");
  static_assert(std::is_trivially_destructible_v<T>, "T must be trivially destructible");
  static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
  static_assert(
    sizeof(T) > sizeof(std::uintptr_t),
    "For types T with a size less or equal to a pointer use an atomic<T> with a compare_exchange update loop.");

  static constexpr unsigned slots = parameter::value_param_t<unsigned, policy::slots, 1, Policies...>::value;
  static_assert(slots >= 1, "slots must be >= 1");

  /**
   * @brief Constructs an empty object.
   */
  seqlock() = default;

  /**
   * @brief Constructs an object of type T via copy construction.
   */
  explicit seqlock(const T& data) { new (&_data[0]) T(data); }

  /**
   * @brief Constructs an object of type T using args as the parameter list for the constructor of T.
   *
   * The object is constructed as if by the expression `new (p) T(std::forward<Args>(args)...)`,
   * where `p` is an internal `void*` pointer to storage suitable to hold an object of type T.
   */
  template <class... Args>
  explicit seqlock(Args&&... args) {
    new (&_data[0]) T(std::forward<Args>(args)...);
  }

  seqlock(const seqlock&) = delete;
  seqlock(seqlock&&) = delete;

  seqlock& operator=(const seqlock&) = delete;
  seqlock& operator=(seqlock&&) = delete;

  /**
   * @brief Reads the current value.
   *
   * Progress guarantees: lock-free if slots > 1; otherwise blocking
   *
   * @return A consistent snapshot of the stored value.
   */
  T load() const;

  /**
   * @brief Stores the given value.
   *
   * Progress guarantees: blocking
   *
   * @param value the new value to be stored.
   */
  void store(const T& value);

  /**
   * @brief Updates the stored value with the given functor.
   *
   * The functor should have the following signature `void(T&) noexcept`, i.e., it should
   * take the current value by reference and perform any modifications directly on that object.
   * *Note:* The functor _must not_ throw any exceptions.
   *
   * Progress guarantees: blocking
   *
   * @param func the functor to update the currently stored value.
   */
  template <class Func>
  void update(Func func);

private:
  using storage_t = typename std::aligned_storage<sizeof(T), alignof(T)>::type;
  using sequence_t = uintptr_t;
  using copy_t = uintptr_t;

  [[nodiscard]] bool is_write_pending(sequence_t seq) const { return (seq & 1) != 0; }

  sequence_t acquire_lock();
  void release_lock(sequence_t seq);

  void read_data(T& dest, const storage_t& src) const;
  void store_data(const T& src, storage_t& dest);

  std::atomic<sequence_t> _seq{0};
  storage_t _data[slots];
};

template <class T, class... Policies>
T seqlock<T, Policies...>::load() const {
  T result;
  // (1) - this acquire-load synchronizes-with the release-store (5)
  sequence_t seq = _seq.load(std::memory_order_acquire);
  for (;;) {
    unsigned idx;
    if constexpr (slots == 1) {
      // wait while update is in progress...
      // this is only necessary if we have a single slot, since otherwise we let
      // reader and writer operate on separate slots.
      while (is_write_pending(seq)) {
        // (2) - this acquire-load synchronizes-with the release-store (5)
        seq = _seq.load(std::memory_order_acquire);
      }
      idx = 0;
    } else {
      seq >>= 1;
      idx = seq % slots;
      seq <<= 1; // we have to ignore a potential write flag here
    }

    read_data(result, _data[idx]);

    // (3) - this acquire-load synchronizes-with the release-store (5)
    auto seq2 = _seq.load(std::memory_order_acquire);
    if (seq2 - seq < (2 * slots - 1)) {
      break;
    }
    seq = seq2;
  }
  return result;
}

template <class T, class... Policies>
template <class Func>
void seqlock<T, Policies...>::update(Func func) {
  auto seq = acquire_lock();
  T data;
  auto idx = (seq >> 1) % slots;
  read_data(data, _data[idx]);
  func(data);
  store_data(data, _data[(idx + 1) % slots]);
  release_lock(seq);
}

template <class T, class... Policies>
void seqlock<T, Policies...>::store(const T& value) {
  auto seq = acquire_lock();
  auto idx = ((seq >> 1) + 1) % slots;
  store_data(value, _data[idx]);
  release_lock(seq);
}

template <class T, class... Policies>
auto seqlock<T, Policies...>::acquire_lock() -> sequence_t {
  auto seq = _seq.load(std::memory_order_relaxed);
  for (;;) {
    while (is_write_pending(seq)) {
      seq = _seq.load(std::memory_order_relaxed);
    }

    assert(is_write_pending(seq) == false);
    // (4) - this acquire-CAS synchronizes-with the release-store (5)
    if (_seq.compare_exchange_weak(seq, seq + 1, std::memory_order_acquire, std::memory_order_relaxed)) {
      return seq + 1;
    }
  }
}

template <class T, class... Policies>
void seqlock<T, Policies...>::release_lock(sequence_t seq) {
  assert(seq == _seq.load(std::memory_order_relaxed));
  assert(is_write_pending(seq));
  // (5) - this release-store synchronizes-with acquire-CAS (4)
  //       and the acquire-load (1, 2, 3)
  _seq.store(seq + 1, std::memory_order_release);
}

template <class T, class... Policies>
void seqlock<T, Policies...>::read_data(T& dest, const storage_t& src) const {
  auto* pdest = reinterpret_cast<copy_t*>(&dest);
  auto* pend = pdest + (sizeof(T) / sizeof(copy_t));
  const auto* psrc = reinterpret_cast<const std::atomic<copy_t>*>(&src);
  for (; pdest != pend; ++psrc, ++pdest) {
    *pdest = psrc->load(std::memory_order_relaxed);
  }
  // (6) - this acquire-fence synchronizes-with the release-fence (7)
  std::atomic_thread_fence(std::memory_order_acquire);

  // Effectively this fence transforms the previous relaxed-loads into acquire-loads. This
  // is necessary to enforce an order with the subsequent load of _seq, so that these
  // relaxed-loads cannot be reordered with the load on _seq. This also implies that if
  // one of these relaxed-loads returns a new value written by a concurrent update operation,
  // the fences synchronize with each other, so it is guaranteed that the subsequent load on
  // _seq "sees" the new sequence value and the load operation will perform a retry.
}

template <class T, class... Policies>
void seqlock<T, Policies...>::store_data(const T& src, storage_t& dest) {
  // (7) - this release-fence synchronizes-with the acquire-fence (6)
  std::atomic_thread_fence(std::memory_order_release);

  const auto* psrc = reinterpret_cast<const copy_t*>(&src);
  const auto* pend = psrc + (sizeof(T) / sizeof(copy_t));
  auto* pdest = reinterpret_cast<std::atomic<copy_t>*>(&dest);
  for (; psrc != pend; ++psrc, ++pdest) {
    pdest->store(*psrc, std::memory_order_relaxed);
  }
}

} // namespace xenium

#endif
