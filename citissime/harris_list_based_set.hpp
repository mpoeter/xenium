#ifndef CITISSIME_HARRIS_LIST_BASED_SET_HPP
#define CITISSIME_HARRIS_LIST_BASED_SET_HPP

#include "reclamation/acquire_guard.hpp"
#include "backoff.hpp"

namespace citissime {

template <class Key, class Reclaimer, class Backoff = no_backoff>
class harris_list_based_set
{
public:
  harris_list_based_set() = default;
  ~harris_list_based_set();

  class iterator;

  bool search(const Key& key);
  bool remove(const Key& key);
  bool insert(Key key);

  iterator begin();
  iterator end();

private:
  struct node;

  using concurrent_ptr = typename Reclaimer::template concurrent_ptr<node, 1>;
  using marked_ptr = typename concurrent_ptr::marked_ptr;
  using guard_ptr = typename concurrent_ptr::guard_ptr;

  struct node : Reclaimer::template enable_concurrent_ptr<node, 1>
  {
    const Key key;
    concurrent_ptr next;
    node(Key k) : key(std::move(k)), next() {}
  };
  
  struct find_info
  {
    concurrent_ptr* prev;
    marked_ptr next;
    guard_ptr cur;
    guard_ptr save;
  };
  bool find(const Key& key, find_info& info, Backoff& backoff);

  concurrent_ptr head;
};

template <class Key, class Reclaimer, class Backoff>
class harris_list_based_set<Key, Reclaimer, Backoff>::iterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = Key;
  using difference_type = std::ptrdiff_t;
  using pointer = const Key *const;
  using reference = const Key&;

  iterator& operator++()
  {
    assert(cur.get() != nullptr);
    auto next = cur->next.load(std::memory_order_acquire);
    guard_ptr tmp_guard;
    if (next.mark() == 0 && tmp_guard.acquire_if_equal(cur->next, next, std::memory_order_acquire))
    {
      prev = &cur->next;
      save = std::move(cur);
      cur = std::move(tmp_guard);
    }
    else
    {
      // cur is marked for removal -> use find to get to the next node with a key >= cur->key
      auto key = cur->key;
      cur.reset();

      find_info info{prev};
      info.save = std::move(save);
      Backoff backoff;
      list.find(key, info, backoff);

      prev = info.prev;
      cur = std::move(info.cur);
      save = std::move(info.save);
    }
    assert(prev == &list.head || cur.get() == nullptr || (save.get() != nullptr && &save->next == prev));
    return *this;
  }
  iterator operator++(int)
  {
    iterator retval = *this;
    ++(*this);
    return retval;
  }
  bool operator==(const iterator& other) const { return cur.get() == other.cur.get(); }
  bool operator!=(const iterator& other) const { return !(*this == other); }
  reference operator*() const { return cur->key; }

private:
  friend harris_list_based_set;

  explicit iterator(harris_list_based_set& list, concurrent_ptr* start) : list(list), prev(start), cur(), save()
  {
    if (prev)
      cur.acquire(*prev, std::memory_order_acquire);
  }

  explicit iterator(harris_list_based_set& list, find_info& info) :
    list(list),
    prev(info.prev),
    cur(std::move(info.cur)),
    save(std::move(info.save))
  {}

  harris_list_based_set& list;
  concurrent_ptr* prev;
  guard_ptr cur;
  guard_ptr save;
};

template <class Key, class Reclaimer, class Backoff>
harris_list_based_set<Key, Reclaimer, Backoff>::~harris_list_based_set()
{
  // delete all remaining nodes
  auto p = head.load(std::memory_order_relaxed);
  while (p)
  {
    auto next = p->next.load(std::memory_order_relaxed);
    delete p.get();
    p = next;
  }
}

template <class Key, class Reclaimer, class Backoff>
bool harris_list_based_set<Key, Reclaimer, Backoff>::find(const Key& key, find_info& info, Backoff& backoff)
{
  assert((info.save == nullptr && info.prev == &head) || &info.save->next == info.prev);
  concurrent_ptr* start = info.prev;
  guard_ptr start_guard = info.save; // we have to keep a guard_ptr to prevent start's node from getting reclaimed.
retry:
  info.prev = start;
  info.save = start_guard;
  info.next = info.prev->load(std::memory_order_relaxed);
  if (info.next.mark() != 0) {
    // our start node is marked for removal -> we have to restart from head
    start = &head;
    start_guard.reset();
    goto retry;
  }

  for (;;)
  {
    // (1) - this acquire-load synchronizes-with the release-CAS (3, 4, 6)
    if (!info.cur.acquire_if_equal(*info.prev, info.next, std::memory_order_acquire))
      goto retry;

    if (!info.cur)
      return false;

    info.next = info.cur->next.load(std::memory_order_relaxed);
    if (info.next.mark() != 0)
    {
      // Node *cur is marked for deletion -> update the link and retire the element

      // (2) - this acquire-load synchronizes-with the release-CAS (3, 4, 6)
      info.next = info.cur->next.load(std::memory_order_acquire).get();

      // Try to splice out node
      marked_ptr expected = info.cur.get();
      // (3) - this release-CAS synchronizes with the acquire-load (1, 2)
      //       it is the head of a potential release sequence containing (5)
      if (!info.prev->compare_exchange_weak(expected, info.next,
                                            std::memory_order_release,
                                            std::memory_order_relaxed))
      {
        backoff();
        goto retry;
      }
      info.cur.reclaim();
    }
    else
    {
      if (info.prev->load(std::memory_order_relaxed) != info.cur.get())
        goto retry; // cur might be cut from list.

      Key ckey = info.cur->key;
      if (ckey >= key)
        return ckey == key;

      info.prev = &info.cur->next;
      std::swap(info.save, info.cur);
    }
  }
}

template <class Key, class Reclaimer, class Backoff>
bool harris_list_based_set<Key, Reclaimer, Backoff>::search(const Key& key)
{
  find_info info{&head};
  Backoff backoff;
  return find(key, info, backoff);
}

template <class Key, class Reclaimer, class Backoff>
bool harris_list_based_set<Key, Reclaimer, Backoff>::insert(Key key)
{
  node* n = new node(std::move(key));
  find_info info{&head};
  Backoff backoff;
  for (;;)
  {
    if (find(key, info, backoff))
    {
      delete n;
      return false;
    }
    // Try to install new node
    marked_ptr cur = info.cur.get();
    n->next.store(cur, std::memory_order_relaxed);

    // (4) - this release-CAS synchronizes with the acquire-load (1, 2)
    //       it is the head of a potential release sequence containing (5)
    if (info.prev->compare_exchange_weak(cur, n,
                                         std::memory_order_release,
                                         std::memory_order_relaxed))
      return true;

    backoff();
  }
}

template <class Key, class Reclaimer, class Backoff>
bool harris_list_based_set<Key, Reclaimer, Backoff>::remove(const Key& key)
{
  Backoff backoff;
  find_info info{&head};
  // Find node in list with matching key and mark it for reclamation.
  for (;;)
  {
    if (!find(key, info, backoff))
      return false; // No such node in the list

    // (5) - this CAS operation is part of a release sequence headed by (3, 4, 6)
    if (info.cur->next.compare_exchange_weak(info.next,
                                             marked_ptr(info.next.get(), 1),
                                             std::memory_order_relaxed))
      break;

    backoff();
  }

  // Try to splice out node
  marked_ptr expected = info.cur;
  // (6) - this release-CAS synchronizes with the acquire-load (1, 2)
  //       it is the head of a potential release sequence containing (5)
  if (info.prev->compare_exchange_weak(expected, info.next,
                                       std::memory_order_release,
                                       std::memory_order_relaxed))
    info.cur.reclaim();
  else
    // Another thread interfered -> rewalk the list to ensure reclamation of marked node before returning.
    find(key, info, backoff);

  return true;
}

template <class Key, class Reclaimer, class Backoff>
auto harris_list_based_set<Key, Reclaimer, Backoff>::begin() -> iterator
{
  return iterator(*this, &head);
}

template <class Key, class Reclaimer, class Backoff>
auto harris_list_based_set<Key, Reclaimer, Backoff>::end() -> iterator
{
  return iterator(*this, nullptr);
}

}

#endif
