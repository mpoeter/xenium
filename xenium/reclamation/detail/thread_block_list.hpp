//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_DETAIL_THREAD_BLOCK_LIST_HPP
#define XENIUM_DETAIL_THREAD_BLOCK_LIST_HPP

#include <atomic>
#include <iterator>

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif

namespace xenium::reclamation::detail {

template <typename T, typename DeletableObject = detail::deletable_object>
class thread_block_list {
  enum class entry_state { free, inactive, active };

public:
  struct entry {
    entry() : next_entry(nullptr), state(entry_state::active) {}

    // Normally this load operation can use relaxed semantic, as all reclamation schemes
    // that use it have an acquire-fence that is sequenced-after calling is_active.
    // However, TSan does not support acquire-fences, so in order to avoid false
    // positives we have to allow other memory orders as well.
    [[nodiscard]] bool is_active(std::memory_order memory_order = std::memory_order_relaxed) const {
      return state.load(memory_order) == entry_state::active;
    }

    void abandon() {
      assert(is_active());
      // (1) - this release-store synchronizes-with the acquire-CAS (2)
      //       or any acquire-fence that is sequenced-after calling is_active.
      state.store(entry_state::free, std::memory_order_release);
    }

    void activate() {
      assert(state.load(std::memory_order_relaxed) == entry_state::inactive);
      state.store(entry_state::active, std::memory_order_release);
    }

  private:
    friend class thread_block_list;

    bool try_adopt(entry_state initial_state) {
      if (state.load(std::memory_order_relaxed) == entry_state::free) {
        auto expected = entry_state::free;
        // (2) - this acquire-CAS synchronizes-with the release-store (1)
        return state.compare_exchange_strong(expected, initial_state, std::memory_order_acquire);
      }
      return false;
    }

    // next_entry is only set once when it gets inserted into the list and is never changed afterwards
    // -> therefore it does not have to be atomic
    T* next_entry;

    // state is used to manage ownership and active status of entries
    std::atomic<entry_state> state;
  };

  class iterator {
    T* ptr = nullptr;

    explicit iterator(T* ptr) : ptr(ptr) {}

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    iterator() = default;

    void swap(iterator& other) noexcept { std::swap(ptr, other.ptr); }

    iterator& operator++() {
      assert(ptr != nullptr);
      ptr = ptr->next_entry;
      return *this;
    }

    iterator operator++(int) {
      assert(ptr != nullptr);
      iterator tmp(*this);
      ptr = ptr->next_entry;
      return tmp;
    }

    bool operator==(const iterator& rhs) const { return ptr == rhs.ptr; }

    bool operator!=(const iterator& rhs) const { return ptr != rhs.ptr; }

    T& operator*() const {
      assert(ptr != nullptr);
      return *ptr;
    }

    T* operator->() const {
      assert(ptr != nullptr);
      return ptr;
    }

    friend class thread_block_list;
  };

  T* acquire_entry() { return adopt_or_create_entry(entry_state::active); }

  T* acquire_inactive_entry() { return adopt_or_create_entry(entry_state::inactive); }

  void release_entry(T* entry) { entry->abandon(); }

  iterator begin() {
    // (3) - this acquire-load synchronizes-with the release-CAS (6)
    return iterator{head.load(std::memory_order_acquire)};
  }

  iterator end() { return iterator{}; }

  void abandon_retired_nodes(DeletableObject* obj) {
    auto* last = obj;
    auto* next = last->next;
    while (next) {
      last = next;
      next = last->next;
    }

    auto* h = abandoned_retired_nodes.load(std::memory_order_relaxed);
    do {
      last->next = h;
      // (4) - this releas-CAS synchronizes-with the acquire-exchange (5)
    } while (
      !abandoned_retired_nodes.compare_exchange_weak(h, obj, std::memory_order_release, std::memory_order_relaxed));
  }

  DeletableObject* adopt_abandoned_retired_nodes() {
    if (abandoned_retired_nodes.load(std::memory_order_relaxed) == nullptr) {
      return nullptr;
    }

    // (5) - this acquire-exchange synchronizes-with the release-CAS (4)
    return abandoned_retired_nodes.exchange(nullptr, std::memory_order_acquire);
  }

private:
  void add_entry(T* node) {
    auto* h = head.load(std::memory_order_relaxed);
    do {
      node->next_entry = h;
      // (6) - this release-CAS synchronizes-with the acquire-loads (3, 7)
    } while (!head.compare_exchange_weak(h, node, std::memory_order_release, std::memory_order_relaxed));
  }

  T* adopt_or_create_entry(entry_state initial_state) {
    static_assert(std::is_base_of<entry, T>::value, "T must derive from entry.");

    // (7) - this acquire-load synchronizes-with the release-CAS (6)
    T* result = head.load(std::memory_order_acquire);
    while (result) {
      if (result->try_adopt(initial_state)) {
        return result;
      }

      result = result->next_entry;
    }

    result = new T();
    result->state.store(initial_state, std::memory_order_relaxed);
    add_entry(result);
    return result;
  }

  std::atomic<T*> head{nullptr};

  alignas(64) std::atomic<DeletableObject*> abandoned_retired_nodes{nullptr};
};

} // namespace xenium::reclamation::detail

#ifdef _MSC_VER
  #pragma warning(pop)
#endif

#endif
