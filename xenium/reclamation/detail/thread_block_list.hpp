#ifndef XENIUM_DETAIL_THREAD_BLOCK_LIST_HPP
#define XENIUM_DETAIL_THREAD_BLOCK_LIST_HPP

#include <atomic>
#include <iterator>

namespace xenium { namespace reclamation { namespace detail {

template <typename T, typename DeletableObject = detail::deletable_object>
class thread_block_list
{
public:
  struct entry
  {
    entry() :
      in_use(true),
      next_entry(nullptr)
    {}

    bool is_active() const { return in_use.load(std::memory_order_relaxed); }
    void abandon() {
      // (1) - this release-store synchronizes-with the acquire-CAS (2)
      in_use.store(false, std::memory_order_release);
    }

  private:
    friend class thread_block_list;

    bool try_adopt()
    {
      if (!in_use.load(std::memory_order_relaxed))
      {
        bool expected = false;
        // (2) - this acquire-CAS synchronizes-with the release-store (1)
        return in_use.compare_exchange_strong(expected, true, std::memory_order_acquire);
      }
      return false;
    }

    // next_entry is only set once when it gets inserted into the list and is never changed afterwards
    // -> therefore it does not have to be atomic
    T* next_entry;

    // in_use is only used to manage ownership of entries
    // -> therefore all operations on it can use relaxed order
    std::atomic<bool> in_use;
  };

  class iterator : public std::iterator<std::forward_iterator_tag, T>
  {
    T* ptr = nullptr;

    explicit iterator(T* ptr) : ptr(ptr) {}
  public:

    iterator() = default;

    void swap(iterator& other) noexcept
    {
        std::swap(ptr, other.ptr);
    }

    iterator& operator++ ()
    {
        assert(ptr != nullptr);
        ptr = ptr->next_entry;
        return *this;
    }

    iterator operator++ (int)
    {
        assert(ptr != nullptr);
        iterator tmp(*this);
        ptr = ptr->next_entry;
        return tmp;
    }

    bool operator == (const iterator& rhs) const
    {
        return ptr == rhs.ptr;
    }

    bool operator != (const iterator& rhs) const
    {
        return ptr != rhs.ptr;
    }

    T& operator* () const
    {
        assert(ptr != nullptr);
        return *ptr;
    }

    T* operator-> () const
    {
        assert(ptr != nullptr);
        return ptr;
    }

    friend class thread_block_list;
  };

  T* acquire_entry()
  {
    static_assert(std::is_base_of<entry, T>::value, "T must derive from entry.");
    return adopt_or_create_entry();
  }

  void release_entry(T* entry)
  {
    entry->abandon();
  }

  iterator begin()
  {
    // (3) - this acquire-load synchronizes-with the release-CAS (6)
    return iterator{head.load(std::memory_order_acquire)};
  }

  iterator end() { return iterator{}; }

  void abandon_retired_nodes(DeletableObject* obj)
  {
    auto last = obj;
    auto next = last->next;
    while (next)
    {
      last = next;
      next = last->next;
    }

    auto h = abandoned_retired_nodes.load(std::memory_order_relaxed);
    do
    {
      last->next = h;
      // (4) - this releas-CAS synchronizes-with the acquire-exchange (5)
    } while (!abandoned_retired_nodes.compare_exchange_weak(h, obj,
        std::memory_order_release, std::memory_order_relaxed));
  }

  DeletableObject* adopt_abandoned_retired_nodes()
  {
    if (abandoned_retired_nodes.load(std::memory_order_relaxed) == nullptr)
      return nullptr;

    // (5) - this acquire-exchange synchronizes-with the release-CAS (4)
    return abandoned_retired_nodes.exchange(nullptr, std::memory_order_acquire);
  }

private:
  void add_entry(T* node)
  {
    auto h = head.load(std::memory_order_relaxed);
    do
    {
      node->next_entry = h;
      // (6) - this release-CAS synchronizes-with the acquire-loads (3, 7)
    } while (!head.compare_exchange_weak(h, node, std::memory_order_release, std::memory_order_relaxed));
  }

  T* adopt_or_create_entry()
  {
    // (7) - this acquire-load synchronizes-with the release-CAS (6)
    T* result = head.load(std::memory_order_acquire);
    while (result)
    {
      if (result->try_adopt())
        return result;

      result = result->next_entry;
    }

    result = new T();
    add_entry(result);
    return result;
  }

  std::atomic<T*> head;

  alignas(64) std::atomic<DeletableObject*> abandoned_retired_nodes;
};

}}}

#endif

