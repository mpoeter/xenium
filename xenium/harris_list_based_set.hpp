#ifndef XENIUM_HARRIS_LIST_BASED_SET_HPP
#define XENIUM_HARRIS_LIST_BASED_SET_HPP

#include <xenium/acquire_guard.hpp>
#include <xenium/backoff.hpp>

namespace xenium {

template <class Key, class Reclaimer, class Backoff = no_backoff>
class harris_list_based_set
{
public:
  harris_list_based_set() = default;
  ~harris_list_based_set();

  class iterator;

  template <class... Args>
  bool emplace(Args&&... args);

  template <class... Args>
  std::pair<iterator, bool> emplace_or_get(Args&&... args);

  bool erase(const Key& key);
  iterator erase(iterator pos);

  iterator find(const Key& key);
  bool contains(const Key& key);

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
    template< class... Args >
    node(Args&&... args) : key(std::forward<Args>(args)...), next() {}
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
  using pointer = const Key*;
  using reference = const Key&;

  iterator(iterator&&) = default;
  iterator(const iterator&) = default;

  iterator& operator=(iterator&&) = default;
  iterator& operator=(const iterator&) = default;

  iterator& operator++()
  {
    assert(info.cur.get() != nullptr);
    auto next = info.cur->next.load(std::memory_order_relaxed);
    guard_ptr tmp_guard;
    // (1) - this acquire-load synchronizes-with the release-CAS (7, 8, 10, 12)
    if (next.mark() == 0 && tmp_guard.acquire_if_equal(info.cur->next, next, std::memory_order_acquire))
    {
      info.prev = &info.cur->next;
      info.save = std::move(info.cur);
      info.cur = std::move(tmp_guard);
    }
    else
    {
      // cur is marked for removal
      // -> use find to remove it and get to the next node with a key >= cur->key
      auto key = info.cur->key;
      Backoff backoff;
      list->find(key, info, backoff);
    }
    assert(info.prev == &list->head ||
           info.cur.get() == nullptr ||
           (info.save.get() != nullptr && &info.save->next == info.prev));
    return *this;
  }
  iterator operator++(int)
  {
    iterator retval = *this;
    ++(*this);
    return retval;
  }
  bool operator==(const iterator& other) const { return info.cur.get() == other.info.cur.get(); }
  bool operator!=(const iterator& other) const { return !(*this == other); }
  reference operator*() const noexcept { return info.cur->key; }
  pointer operator->() const noexcept { return &info.cur->key; }

private:
  friend harris_list_based_set;

  explicit iterator(harris_list_based_set& list, concurrent_ptr* start) : list(&list)
  {
    info.prev = start;
    if (start) {
      // (2) - this acquire-load synchronizes-with the release-CAS (7, 8, 10, 12)
      info.cur.acquire(*start, std::memory_order_acquire);
    }
  }

  explicit iterator(harris_list_based_set& list, find_info&& info) :
    list(&list),
    info(std::move(info))
  {}

  harris_list_based_set* list;
  find_info info;
};

template <class Key, class Reclaimer, class Backoff>
harris_list_based_set<Key, Reclaimer, Backoff>::~harris_list_based_set()
{
  // delete all remaining nodes
  // (3) - this acquire-load synchronizes-with the release-CAS (7, 8, 10, 12)
  auto p = head.load(std::memory_order_acquire);
  while (p)
  {
    // (4) - this acquire-load synchronizes-with the release-CAS (7, 8, 10, 12)
    auto next = p->next.load(std::memory_order_acquire);
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
    // (5) - this acquire-load synchronizes-with the release-CAS (7, 8, 10, 12)
    if (!info.cur.acquire_if_equal(*info.prev, info.next, std::memory_order_acquire))
      goto retry;

    if (!info.cur)
      return false;

    info.next = info.cur->next.load(std::memory_order_relaxed);
    if (info.next.mark() != 0)
    {
      // Node *cur is marked for deletion -> update the link and retire the element

      // (6) - this acquire-load synchronizes-with the release-CAS (7, 8, 10, 12)
      info.next = info.cur->next.load(std::memory_order_acquire).get();

      // Try to splice out node
      marked_ptr expected = info.cur.get();
      // (7) - this release-CAS synchronizes with the acquire-load (1, 2, 3, 4, 5, 6)
      //       and the require-CAS (9, 11)
      //       it is the head of a potential release sequence containing (9, 11)
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
bool harris_list_based_set<Key, Reclaimer, Backoff>::contains(const Key& key)
{
  find_info info{&head};
  Backoff backoff;
  return find(key, info, backoff);
}

template <class Key, class Reclaimer, class Backoff>
auto harris_list_based_set<Key, Reclaimer, Backoff>::find(const Key& key) -> iterator
{
  find_info info{&head};
  Backoff backoff;
  if (find(key, info, backoff))
    return iterator(*this, std::move(info));
  return end();
}

template <class Key, class Reclaimer, class Backoff>
template <class... Args>
bool harris_list_based_set<Key, Reclaimer, Backoff>::emplace(Args&&... args)
{
  auto result = emplace_or_get(std::forward<Args>(args)...);
  return result.second;
}

template <class Key, class Reclaimer, class Backoff>
template <class... Args>
auto harris_list_based_set<Key, Reclaimer, Backoff>::emplace_or_get(Args&&... args) -> std::pair<iterator, bool>
{
  node* n = new node(std::forward<Args>(args)...);
  find_info info{&head};
  Backoff backoff;
  for (;;)
  {
    if (find(n->key, info, backoff))
    {
      delete n;
      return {iterator(*this, std::move(info)), false};
    }
    // Try to install new node
    marked_ptr cur = info.cur.get();
    info.cur.reset();
    info.cur = guard_ptr(n);
    n->next.store(cur, std::memory_order_relaxed);

    // (8) - this release-CAS synchronizes with the acquire-load (1, 2, 3, 4, 5, 6)
    //       and the acquire-CAS (9, 11)
    //       it is the head of a potential release sequence containing (9, 11)
    if (info.prev->compare_exchange_weak(cur, n,
                                         std::memory_order_release,
                                         std::memory_order_relaxed))
      return {iterator(*this, std::move(info)), true};

    backoff();
  }
}

template <class Key, class Reclaimer, class Backoff>
bool harris_list_based_set<Key, Reclaimer, Backoff>::erase(const Key& key)
{
  Backoff backoff;
  find_info info{&head};
  // Find node in list with matching key and mark it for reclamation.
  for (;;)
  {
    if (!find(key, info, backoff))
      return false; // No such node in the list

    // (9) - this acquire-CAS synchronizes with the release-CAS (7, 8, 10, 12)
    //       and is part of a release sequence headed by those operations
    if (info.cur->next.compare_exchange_weak(info.next,
                                             marked_ptr(info.next.get(), 1),
                                             std::memory_order_acquire,
                                             std::memory_order_relaxed))
      break;

    backoff();
  }

  // Try to splice out node
  marked_ptr expected = info.cur;
  // (10) - this release-CAS synchronizes with the acquire-load (1, 2, 3, 4, 5, 6)
  //        and the acquire-CAS (9, 11)
  //        it is the head of a potential release sequence containing (9, 11)
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
auto harris_list_based_set<Key, Reclaimer, Backoff>::erase(iterator pos) -> iterator
{
  Backoff backoff;
  auto next = pos.info.cur->next.load(std::memory_order_relaxed);
  for (;;)
  {
    // (11) - this acquire-CAS synchronizes-with the release-CAS (7, 8, 10, 12)
    //        and is part of a release sequence headed by those operations
    if (pos.info.cur->next.compare_exchange_weak(next,
                                            marked_ptr(next.get(), 1),
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed))
      break;

    backoff();
  }

  guard_ptr next_guard(next);

  // Try to splice out node
  marked_ptr expected = pos.info.cur;
  // (12) - this release-CAS synchronizes with the acquire-load (1, 2, 3, 4, 5, 6)
  //        and the acquire-CAS (9, 11)
  //        it is the head of a potential release sequence containing (9, 11)
  if (pos.info.prev->compare_exchange_weak(expected, next,
                                      std::memory_order_release,
                                      std::memory_order_relaxed)) {
    pos.info.cur.reclaim();
    pos.info.cur = std::move(next_guard);
  } else {
    next_guard.reset();
    Key key = pos.info.cur->key;

    // Another thread interfered -> rewalk the list to ensure reclamation of marked node before returning.
    find(key, pos.info, backoff);
  }

  return pos;
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
