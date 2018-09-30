#ifndef XENIUM_MICHAEL_HARRIS_HASH_MAP_HPP
#define XENIUM_MICHAEL_HARRIS_HASH_MAP_HPP

#include <xenium/acquire_guard.hpp>
#include <xenium/backoff.hpp>

#include <atomic>
#include <functional>

namespace xenium {

template <class Key, class Value, class Reclaimer, size_t Buckets, class Backoff = no_backoff>
class michael_harris_hash_map
{
public:
  using value_type = std::pair<const Key, Value>;

  class iterator;

  michael_harris_hash_map() = default;
  ~michael_harris_hash_map();

  template <class... Args>
  bool emplace(Args&&... args);

  template <class... Args>
  std::pair<iterator, bool> emplace_or_get(Args&&... args);

  template <typename Func>
  std::pair<iterator, bool> get_or_insert(Key key, Func value_factory);

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
  public:
    value_type value;
  private:
    concurrent_ptr next;
    template< class... Args >
    node(Args&&... args) : value(std::forward<Args>(args)...), next() {}
    friend class michael_harris_hash_map;
  };

  struct find_info
  {
    concurrent_ptr* prev;
    marked_ptr next;
    guard_ptr cur;
    guard_ptr save;
  };

  bool find(const Key& key, std::size_t bucket, find_info& info, Backoff& backoff);

  concurrent_ptr buckets[Buckets];
};

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
class michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::iterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = michael_harris_hash_map::value_type;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type*;
  using reference = value_type&;

  iterator(iterator&&) = default;
  iterator(const iterator&) = default;

  iterator& operator=(iterator&&) = default;
  iterator& operator=(const iterator&) = default;

  iterator& operator++()
  {
    assert(info.cur.get() != nullptr);
    auto next = info.cur->next.load(std::memory_order_acquire);
    guard_ptr tmp_guard;
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
      Key key = info.cur->value.first;
      Backoff backoff;
      map->find(key, bucket, info, backoff);
    }
    assert(info.prev == &map->buckets[bucket] ||
           info.cur.get() == nullptr ||
           (info.save.get() != nullptr && &info.save->next == info.prev));

    if (!info.cur)
      move_to_next_bucket();

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
  reference operator*() const noexcept { return info.cur->value; }
  pointer operator->() const noexcept { return &info.cur->value; }

  void reset() {
    bucket = Buckets;
    info.prev = nullptr;
    info.cur.reset();
    info.save.reset();
  }

private:
  friend michael_harris_hash_map;

  explicit iterator(michael_harris_hash_map* map) :
    map(map),
    bucket(Buckets)
  {}

  explicit iterator(michael_harris_hash_map* map, std::size_t bucket) :
    map(map),
    bucket(bucket)
  {
    info.prev = &map->buckets[bucket];
    info.cur.acquire(*info.prev, std::memory_order_acquire);

    if (!info.cur)
      move_to_next_bucket();
  }

  explicit iterator(michael_harris_hash_map* map, std::size_t bucket, find_info&& info) :
    map(map),
    bucket(bucket),
    info(std::move(info))
  {}

  void move_to_next_bucket() {
    info.save.reset();
    while (!info.cur && bucket < Buckets - 1) {
      ++bucket;
      info.prev = &map->buckets[bucket];
      info.cur.acquire(*info.prev, std::memory_order_acquire);
    }
  }

  michael_harris_hash_map* map;
  std::size_t bucket;
  find_info info;
};

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::~michael_harris_hash_map()
{
  for (size_t i = 0; i < Buckets; ++i)
  {
    auto p = buckets[i].load(std::memory_order_acquire);
    while (p)
    {
      auto next = p->next.load(std::memory_order_acquire);
      delete p.get();
      p = next;
    }
  }
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
bool michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::find(const Key& key, std::size_t bucket,
  find_info& info, Backoff& backoff)
{
  auto& head = buckets[bucket];
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
        goto retry; // cur might be cut from the hash_map.

      Key ckey = info.cur->value.first;
      if (ckey >= key)
        return ckey == key;

      info.prev = &info.cur->next;
      std::swap(info.save, info.cur);
    }
  }
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
bool michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::contains(const Key& key)
{
  auto bucket = std::hash<Key>{}(key) % Buckets;
  find_info info{&buckets[bucket]};
  Backoff backoff;
  return find(key, bucket, info, backoff);
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
auto michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::find(const Key& key) -> iterator
{
  auto bucket = std::hash<Key>{}(key) % Buckets;
  find_info info{&buckets[bucket]};
  Backoff backoff;
  find(key, bucket, info, backoff);
  return iterator(this, bucket, std::move(info));
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
template <class... Args>
bool michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::emplace(Args&&... args)
{
  auto result = emplace_or_get(std::forward<Args>(args)...);
  return result.second;
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
template <typename Func>
auto michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::get_or_insert(Key key, Func value_factory)
  -> std::pair<iterator, bool>
{
  node* n = nullptr;
  auto bucket = std::hash<Key>{}(key) % Buckets;

  const Key* pkey = &key;
  find_info info{&buckets[bucket]};
  Backoff backoff;
  for (;;)
  {
    if (find(*pkey, bucket, info, backoff))
    {
      delete n;
      return {iterator(this, bucket, std::move(info)), false};
    }
    if (n == nullptr) {
      n = new node(std::move(key), value_factory());
      pkey = &n->value.first;
    }

    // Try to install new node
    marked_ptr cur = info.cur.get();
    info.cur.reset();
    info.cur = guard_ptr(n);
    n->next.store(cur, std::memory_order_relaxed);

    // (4) - this release-CAS synchronizes with the acquire-load (1, 2)
    //       it is the head of a potential release sequence containing (5)
    if (info.prev->compare_exchange_weak(cur, n,
                                         std::memory_order_release,
                                         std::memory_order_relaxed))
      return {iterator(this, bucket, std::move(info)), true};

    backoff();
  }
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
template <class... Args>
auto michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::emplace_or_get(Args&&... args)
  -> std::pair<iterator, bool>
{
  node* n = new node(std::forward<Args>(args)...);
  auto bucket = std::hash<Key>{}(n->value.first) % Buckets;

  find_info info{&buckets[bucket]};
  Backoff backoff;
  for (;;)
  {
    if (find(n->value.first, bucket, info, backoff))
    {
      delete n;
      return {iterator(this, bucket, std::move(info)), false};
    }
    // Try to install new node
    marked_ptr cur = info.cur.get();
    info.cur.reset();
    info.cur = guard_ptr(n);
    n->next.store(cur, std::memory_order_relaxed);

    // (4) - this release-CAS synchronizes with the acquire-load (1, 2)
    //       it is the head of a potential release sequence containing (5)
    if (info.prev->compare_exchange_weak(cur, n,
                                         std::memory_order_release,
                                         std::memory_order_relaxed))
      return {iterator(this, bucket, std::move(info)), true};

    backoff();
  }
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
bool michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::erase(const Key& key)
{
  auto bucket = std::hash<Key>{}(key) % Buckets;
  Backoff backoff;
  find_info info{&buckets[bucket]};
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

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
auto michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::erase(iterator pos) -> iterator
{
  Backoff backoff;
  auto next = pos.info.cur->next.load(std::memory_order_relaxed);
  for (;;)
  {
    // (5) - this CAS operation is part of a release sequence headed by (3, 4, 6)
    if (pos.info.cur->next.compare_exchange_weak(next,
                                                 marked_ptr(next.get(), 1),
                                                 std::memory_order_relaxed))
      break;

    backoff();
  }

  guard_ptr next_guard(next);

  // Try to splice out node
  marked_ptr expected = pos.info.cur;
  // (6) - this release-CAS synchronizes with the acquire-load (1, 2)
  //       it is the head of a potential release sequence containing (5)
  if (pos.info.prev->compare_exchange_weak(expected, next,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
    pos.info.cur.reclaim();
    pos.info.cur = std::move(next_guard);
  } else {
    next_guard.reset();
    Key key = pos.info.cur->value.first;

    // Another thread interfered -> rewalk the list to ensure reclamation of marked node before returning.
    find(key, pos.bucket, pos.info, backoff);
  }

  if (!pos.info.cur)
    pos.move_to_next_bucket();

  return pos;
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
auto michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::begin() -> iterator
{
  return iterator(this, 0);
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
auto michael_harris_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::end() -> iterator
{
  return iterator(this);
}

}

#endif
