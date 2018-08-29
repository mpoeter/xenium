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
  
  bool search(const Key& key);
  bool remove(const Key& key);
  bool insert(Key key);

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

  concurrent_ptr head;
  
  struct find_info
  {
    concurrent_ptr* prev;
    marked_ptr next;
    guard_ptr cur;
    guard_ptr save;
  };
  bool find(const Key& key, find_info& info, Backoff& backoff);
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
retry:
  info.prev = &head;
  info.next = info.prev->load(std::memory_order_relaxed);
  info.save.reset();

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
      info.save = std::move(info.cur);
    }
  }
}

template <class Key, class Reclaimer, class Backoff>
bool harris_list_based_set<Key, Reclaimer, Backoff>::search(const Key& key)
{
  find_info info;
  Backoff backoff;
  return find(key, info, backoff);
}

template <class Key, class Reclaimer, class Backoff>
bool harris_list_based_set<Key, Reclaimer, Backoff>::insert(Key key)
{
  node* n = new node(std::move(key));
  find_info info;
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
  find_info info;
  // Find node in list with matching key and mark it for reclamation.
  do
  {
    if (!find(key, info, backoff))
      return false; // No such node in the list
    // (5) - this CAS operation is part of a release sequence headed by (3, 4, 6)
  } while (!info.cur->next.compare_exchange_weak(info.next,
                                                 marked_ptr(info.next.get(), 1),
                                                 std::memory_order_relaxed));

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
}

#endif
