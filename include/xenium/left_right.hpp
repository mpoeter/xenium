//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_LEFT_RIGHT_HPP
#define XENIUM_LEFT_RIGHT_HPP

#include <atomic>
#include <cassert>
#include <mutex>
#include <thread>

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif

namespace xenium {

/**
 * @brief Generic implementation of the LeftRight algorithm proposed by Ramalhete
 * and Correia \[[RC15](index.html#ref-ramalhete-2015)\].
 *
 * The LeftRight algorithm provides the following advantages when compared to a
 * read-write-lock:
 *   * writers never block readers, i.e., read operations are wait-free
 *     (population oblivious)
 *   * readers never block writers, i.e., the updated data is immediately
 *     visible to new readers.
 *
 * This is comes at the cost of a duplication of the underlying data structure,
 * which also means that update operations have to be applied to both instances.
 *
 * @tparam T
 */
template <typename T>
struct left_right {
  /**
   * @brief Initialize the two underlying T instances with the specified `source`.
   *
   * The first instance is copy-constructed while the second one is move-constructed.
   *
   * @param source the source used to initialize the two underlying instances.
   */
  explicit left_right(T source) : _left(source), _right(std::move(source)) {}

  /**
   * @brief Initializes the two underlying instances withe the specified sources.
   *
   * Both instances are move-constructed from the specified sources.
   *
   * @param left the source to initialize the left instance
   * @param right the source to initialize the right instance
   */
  left_right(T left, T right) : _left(std::move(left)), _right(std::move(right)) {}

  /**
   * @brief Default constructs both underlying instances.
   */
  left_right() = default;

  /**
   * @brief Performs a read operation on the active instance using the specified functor.
   *
   * The functor `func` is called for the currently active instance. The instance is passed
   * to the functor as a const reference.
   *
   * This method simply returns the result of the call to `func`, i.e., the return type of
   * this method corresponds to the return type of the `func`;
   *
   * @tparam Func
   * @param func
   * @return the value returned by the call to `func`
   */
  template <typename Func>
  auto read(Func&& func) const {
    read_guard guard(*this);
    // (1) - this seq-cst-load enforces a total order with the seq-cst-store (2, 3)
    const T& inst = _lr_indicator.load(std::memory_order_seq_cst) == READ_LEFT ? _left : _right;
    return func(inst);
  }

  /**
   * @brief Performs an update operation on both underlying instances using the specified functor.
   *
   * The functor `func` is called twice - once for each underlying instance. The instance to be
   * updated is passed as a non-const reference to `func`.
   *
   * @tparam Func
   * @param func
   */
  template <typename Func>
  void update(Func&& func) {
    std::lock_guard<std::mutex> lock(_writer_mutex);
    assert(_lr_indicator.load() == _version_index.load());
    if (_lr_indicator.load(std::memory_order_relaxed) == READ_LEFT) {
      func(_right);
      // (2) - this seq-cst-store enforces a total order with the seq-cst-load (1)
      _lr_indicator.store(READ_RIGHT, std::memory_order_seq_cst);
      toggle_version_and_wait();
      func(_left);
    } else {
      func(_left);
      // (3) - this seq-cst-store enforces a total order with the seq-cst-load (1)
      _lr_indicator.store(READ_LEFT, std::memory_order_seq_cst);
      toggle_version_and_wait();
      func(_right);
    }
  }

private:
  struct alignas(64) read_indicator {
    void arrive() {
      // (4) - this seq-cst-fetch-add enforces a total order with the seq-cst-load (6)
      _counter.fetch_add(1, std::memory_order_seq_cst);
    }
    void depart() {
      // (5) - this release-fetch-sub synchronizes-with the seq-cst-load (6)
      _counter.fetch_sub(1, std::memory_order_release);
      // Note: even though this method is only called by reader threads that (usually)
      // do not change the underlying data structure, we still have to use release
      // order here to ensure that the read operations is properly ordered before a
      // subsequent update operation.
    }
    bool empty() {
      // (6) - this seq-cst-load enforces a total order with the seq-cst-fetch-add (4)
      //       and synchronizes-with the release-fetch-add (5)
      return _counter.load(std::memory_order_seq_cst) == 0;
    }

  private:
    std::atomic<uint64_t> _counter{0};
  };

  struct read_guard {
    explicit read_guard(const left_right& inst) :
        _indicator(inst.get_read_indicator(inst._version_index.load(std::memory_order_relaxed))) {
      _indicator.arrive();
    }
    ~read_guard() { _indicator.depart(); }

  private:
    read_indicator& _indicator;
  };
  friend struct read_guard;

  void toggle_version_and_wait() {
    const int current_version = _version_index.load(std::memory_order_relaxed);
    const int current_idx = current_version & 0x1;
    const int next_idx = (current_version + 1) & 0x1;

    wait_for_readers(next_idx);
    _version_index.store(next_idx, std::memory_order_relaxed);
    wait_for_readers(current_idx);
  }

  void wait_for_readers(int idx) {
    auto& indicator = get_read_indicator(idx);
    while (!indicator.empty()) {
      std::this_thread::yield();
    }
  }

  read_indicator& get_read_indicator(int idx) const {
    assert(idx == 0 || idx == 1);
    if (idx == 0) {
      return _read_indicator1;
    }
    return _read_indicator2;
  }

  static constexpr int READ_LEFT = 0;
  static constexpr int READ_RIGHT = 1;

  // TODO: make mutex type configurable via policy
  std::mutex _writer_mutex;
  std::atomic<int> _version_index{0};
  std::atomic<int> _lr_indicator{READ_LEFT};

  mutable read_indicator _read_indicator1;
  T _left;

  mutable read_indicator _read_indicator2;
  T _right;
};
} // namespace xenium

#ifdef _MSC_VER
  #pragma warning(pop)
#endif

#endif