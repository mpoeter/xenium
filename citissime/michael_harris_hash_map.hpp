#ifndef CITISSIME_MICHAEL_HARRIS_HASH_MAP_HPP
#define CITISSIME_MICHAEL_HARRIS_HASH_MAP_HPP

#include "reclamation/acquire_guard.hpp"
#include "backoff.hpp"

#include <atomic>
#include <functional>

namespace citissime {

template <class Key, class Value, class Reclaimer, size_t Buckets, class Backoff = no_backoff>
class michael_harris_hash_map
{
public:
  struct node;

private:
  using concurrent_ptr = typename Reclaimer::template concurrent_ptr<node, 1>;
  using marked_ptr = typename concurrent_ptr::marked_ptr;

public:
  michael_harris_hash_map() = default;
  ~michael_harris_hash_map();

  using guard_ptr = typename concurrent_ptr::guard_ptr;

  struct node : Reclaimer::template enable_concurrent_ptr<node, 1>
  {
  public:
    const Key key;
    const Value value;
  private:
    concurrent_ptr next;
    node(Key k, Value v) : key(std::move(k)), value(std::move(v)), next() {}
    friend class michael_harris_hash_map;
  };

  guard_ptr search(const Key& key);
  bool insert(Key key, Value value);
  bool insert(Key key, Value value, guard_ptr& entry);
  bool remove(const Key& key);

private:
  concurrent_ptr buckets[Buckets];
  
  struct find_info
  {
    concurrent_ptr* prev;
    marked_ptr next;
    guard_ptr cur;
    guard_ptr save;
  };
  bool find(const Key& key, concurrent_ptr& head, find_info& info, Backoff& backoff);
};

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::~michael_harris_hash_map()
{
  for (size_t i = 0; i < Buckets; ++i)
  {
    auto p = buckets[i].load(std::memory_order_relaxed);
    while (p)
    {
      auto next = p->next.load(std::memory_order_relaxed);
      delete p.get();
      p = next;
    }
  }
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
bool michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::find(const Key& key, concurrent_ptr& head,
  find_info& info, Backoff& backoff)
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
        goto retry; // cur might be cut from the hash_map.

      Key ckey = info.cur->key;
      if (ckey >= key)
        return ckey == key;

      info.prev = &info.cur->next;
      info.save = std::move(info.cur);
    }
  }
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
auto michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::search(const Key& key) -> guard_ptr
{
  find_info info;
  Backoff backoff;
  auto h = std::hash<Key>{}(key);
  if (find(key, buckets[h % Buckets], info, backoff))
    return std::move(info.cur);
  return guard_ptr{};
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
bool michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::insert(Key key, Value value)
{
  guard_ptr dummy;
  return insert(std::move(key), std::move(value), dummy);
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
bool michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::insert(Key key, Value value, guard_ptr& entry)
{
  auto& bucket = buckets[std::hash<Key>{}(key) % Buckets];
  node* n = new node(std::move(key), std::move(value));
  entry = guard_ptr(n);
  find_info info;
  Backoff backoff;
  for (;;)
  {
    if (find(key, bucket, info, backoff))
    {
      entry = std::move(info.cur);
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

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
bool michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::remove(const Key& key)
{
  Backoff backoff;
  find_info info;
  auto& bucket = buckets[std::hash<Key>{}(key) % Buckets];
  // Find node in hash_map with matching key and mark it for erasure .
  do
  {
    if (!find(key, bucket, info, backoff))
      return false; // No such node in the hash_map
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
    // Another thread interfered -> rewalk the bucket's list to ensure
    // reclamation of marked node before returning.
    find(key, bucket, info, backoff);
   
  return true;
}
}

#endif
