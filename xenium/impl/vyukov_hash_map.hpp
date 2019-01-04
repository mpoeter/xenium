//
// Copyright (c) 2018 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_VYUKOV_HASH_MAP_IMPL
#error "This is an impl file and must not be included directly!"
#endif

#include <xenium/acquire_guard.hpp>
#include <xenium/backoff.hpp>
#include <xenium/parameter.hpp>
#include <xenium/policy.hpp>

#include <boost/align/aligned_alloc.hpp>

#include <atomic>
#include <cstring>

namespace xenium { 

template <class Key, class Value, class... Policies>
struct vyukov_hash_map<Key, Value, Policies...>::bucket_state {
  bucket_state() : value() {}

  bucket_state locked() const { return value | lock; }
  bucket_state clear_lock() const {
    assert(value & lock);
    return value ^ lock;
  }
  bucket_state new_version() const { return value + version_inc; }
  bucket_state inc_item_count() const {
    assert(item_count() < bucket_item_count);
    bucket_state result(value + item_count_inc);
    assert(result.item_count() == item_count() + 1);
    return result;
  }
  bucket_state dec_item_count() const {
    assert(item_count() > 0);
    bucket_state result(value - item_count_inc);
    assert(result.item_count() == item_count() - 1);
    return result;
  }
  bucket_state set_delete_marker(std::uint32_t marker) const {
    assert(delete_marker() == 0);
    bucket_state result(value | (marker << delete_marker_shift));
    assert(result.delete_marker() == marker);
    return result;
  }

  bool operator==(bucket_state r) const { return this->value == r.value; }
  bool operator!=(bucket_state r) const { return this->value != r.value; }

  std::uint32_t item_count() const { return (value >> 1) & item_count_mask; }
  std::uint32_t delete_marker() const { return (value >> delete_marker_shift) & item_count_mask; }
  std::uint32_t version() const { return value >> version_shift; }
  bool is_locked() const { return (value & lock) != 0; }

private:
  bucket_state(std::uint32_t value) : value(value) {}

  std::uint32_t value;

  /*
    value has the same structure as the following bit field:
    struct {
      // the lock bit
      unsigned lock : 1;

      // the number of items in the bucket array
      unsigned item_count : find_last_bit_set(bucket_item_count);

      // marker for the item that is currently beeing removed
      unsigned delete_marker : find_last_bit_set(bucket_item_count);

      // version counter - gets incremented at the end of every remove operation
      unsigned version : sizeof(unsigned) * 8 - 2 * find_last_bit_set(bucket_item_count) - 1;
    };
  */

  static constexpr std::size_t item_counter_bits = utils::find_last_bit_set(bucket_item_count);

  static constexpr std::size_t item_count_shift = 1;
  static constexpr std::size_t delete_marker_shift = item_count_shift + item_counter_bits;
  static constexpr std::size_t version_shift = delete_marker_shift + item_counter_bits;

  static constexpr std::uint32_t lock = 1;
  static constexpr std::uint32_t version_inc = 1 << version_shift;
  static constexpr std::uint32_t item_count_inc = 1 << item_count_shift;

  static constexpr std::uint32_t item_count_mask = (1 << item_counter_bits) - 1;
};
  
template <class Key, class Value, class... Policies>
struct vyukov_hash_map<Key, Value, Policies...>::bucket {
  std::atomic<bucket_state> state;
  std::atomic<extension_item*> head;
  std::atomic<Key> key[bucket_item_count];
  std::atomic<Value> value[bucket_item_count];
};

template <class Key, class Value, class... Policies>
struct vyukov_hash_map<Key, Value, Policies...>::extension_item {
  std::atomic<Key> key;
  std::atomic<Value> value;
  std::atomic<extension_item*> next;
};

template <class Key, class Value, class... Policies>
struct vyukov_hash_map<Key, Value, Policies...>::extension_bucket {
  std::atomic<std::uint32_t> lock;
  extension_item* head;
  extension_item items[extension_item_count];

  void acquire_lock() {
    backoff backoff;
    for (;;) {
      while (lock.load(std::memory_order_relaxed))
        ;
      if (lock.exchange(1, std::memory_order_acquire) == 0)
        return;
      backoff();
    }
  }
  void release_lock() { lock.store(0, std::memory_order_release); }
};

template <class Key, class Value, class... Policies>
struct alignas(64) vyukov_hash_map<Key, Value, Policies...>::block :
  reclaimer::template enable_concurrent_ptr<block>
{
  std::uint32_t mask;
  std::uint32_t bucket_count;
  std::uint32_t extension_bucket_count;
  extension_bucket* extension_buckets;

  // TODO - adapt to be customizable via map_to_bucket policy
  std::uint32_t index(Key key) const { return static_cast<std::uint32_t>(key & mask); }
  bucket* buckets() { return reinterpret_cast<bucket*>(this+1); }

  void operator delete(void* p) { boost::alignment::aligned_free(p); }
};

template <class Key, class Value, class... Policies>
vyukov_hash_map<Key, Value, Policies...>::vyukov_hash_map(std::size_t initial_capacity) :
  resize_lock(0)
{
  auto b = allocate_block(utils::next_power_of_two(initial_capacity));
  if (b == nullptr)
    throw std::bad_alloc();
  data_block.store(b, std::memory_order_release);
}

template <class Key, class Value, class... Policies>
vyukov_hash_map<Key, Value, Policies...>::~vyukov_hash_map() {
  // TODO - clean up
  delete data_block.load().get();
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::emplace(Key key, Value value) {
  const hash_t h = hash{}(key);
  
retry:
  guard_ptr b;
  bucket_state state;
  bucket& bucket = lock_bucket(h, b, state);
  std::uint32_t item_count = state.item_count();

  for (std::uint32_t i = 0; i != item_count; ++i)
    if (key == bucket.key[i].load(std::memory_order_relaxed)) {
      bucket.state.store(state, std::memory_order_relaxed);
      return false;
    }

  if (item_count < bucket_item_count) {
    bucket.key[item_count].store(key, std::memory_order_relaxed);
    bucket.value[item_count].store(value, std::memory_order_relaxed);
    // release the bucket lock and increment the item count
    bucket.state.store(state.inc_item_count(), std::memory_order_release);
    return true;
  }

  // TODO - keep extension items sorted
  for (extension_item* extension = bucket.head.load(std::memory_order_relaxed);
       extension != nullptr;
       extension = extension->next.load(std::memory_order_relaxed)) {
    if (key == extension->key.load(std::memory_order_relaxed)) {
      bucket.state.store(state, std::memory_order_relaxed);
      return false;
    }
  }

  extension_item* extension = allocate_extension_item(b.get(), key);
  if (extension == nullptr) {
    grow(bucket, state);
    goto retry;
  }
  extension->key.store(key, std::memory_order_relaxed);
  extension->value.store(value, std::memory_order_relaxed);

  // use release semantic here to ensure that a thread in get() that sees the
  // new pointer also sees the new key/value pair
  auto old_head = bucket.head.load(std::memory_order_relaxed);
  extension->next.store(old_head, std::memory_order_release);
  bucket.head.store(extension, std::memory_order_release);
  // release the bucket lock
  bucket.state.store(state, std::memory_order_release);

  return true;
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::erase(const Key& key) {
  const hash_t h = hash{}(key);
  backoff backoff;
  guard_ptr b;

restart:
  b.acquire(data_block, std::memory_order_acquire);
  const std::size_t bucket_idx = h & b->mask;
  bucket& bucket = b->buckets()[bucket_idx];
  bucket_state state = bucket.state.load(std::memory_order_relaxed);
  std::uint32_t item_count = state.item_count();

  if (item_count == 0)
    return false; // no items to check

  if (state.is_locked())
    goto restart;

  auto locked_state = state.locked();
  if (!bucket.state.compare_exchange_strong(state, locked_state, std::memory_order_acquire,
                                                                 std::memory_order_relaxed)) {
    backoff();
    goto restart;
  }

  // we have the lock - now look for the key

  for (std::uint32_t i = 0; i != item_count; ++i) {
    if (key == bucket.key[i].load(std::memory_order_relaxed)) {
      extension_item* extension = bucket.head.load(std::memory_order_relaxed);
      if (extension) {
        // signal which item we are deleting
        bucket.state.store(locked_state.set_delete_marker(i + 1), std::memory_order_relaxed);

        Key k = extension->key.load(std::memory_order_relaxed);
        Value v = extension->value.load(std::memory_order_relaxed);
        bucket.key[i].store(k, std::memory_order_relaxed);
        bucket.value[i].store(v, std::memory_order_release);
        
        // reset the delete marker
        locked_state = locked_state.new_version();
        bucket.state.store(locked_state, std::memory_order_release);

        extension_item* extension_next = extension->next.load(std::memory_order_relaxed);
        bucket.head.store(extension_next, std::memory_order_release);

        // release the bucket lock and increase the version
        bucket.state.store(locked_state.new_version().clear_lock(), std::memory_order_release);

        free_extension_item(extension);
      } else {
        if (i != item_count - 1) {
          // signal which item we are deleting
          bucket.state.store(locked_state.set_delete_marker(i + 1), std::memory_order_relaxed);

          Key k = bucket.key[item_count - 1].load(std::memory_order_relaxed);
          Value v = bucket.value[item_count - 1].load(std::memory_order_relaxed);
          bucket.key[i].store(k, std::memory_order_relaxed);
          bucket.value[i].store(v, std::memory_order_release);
        }
        
        // release the bucket lock, reset the delete marker (if it is set), increase the version
        // and decrement the item counter.
        bucket.state.store(state.new_version().dec_item_count(), std::memory_order_release);
      }
      return true;
    }
  }

  auto extension_prev = &bucket.head;
  extension_item* extension = extension_prev->load(std::memory_order_relaxed);
  while (extension) {
    if (key == extension->key.load(std::memory_order_relaxed)) {
      extension_item* extension_next = extension->next.load(std::memory_order_relaxed);    
      extension_prev->store(extension_next, std::memory_order_relaxed);

      // release the bucket lock and increase the version
      bucket.state.store(state.new_version(), std::memory_order_release);

      free_extension_item(extension);
      return true;
    }
    extension_prev = &extension->next;
    extension = extension_prev->load(std::memory_order_relaxed);
  }

  // key not found

  // release the bucket lock
  bucket.state.store(state, std::memory_order_relaxed);

  return false;
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::try_get_value(const Key& key, Value& value) const {
  const hash_t h = hash{}(key);

  guard_ptr b = acquire_guard(data_block, std::memory_order_acquire);
  const std::size_t bucket_idx = h & b->mask;
  bucket& bucket = b->buckets()[bucket_idx];
  bucket_state state = bucket.state.load(std::memory_order_acquire);

  // data_block (and therefore the bucket) can only change due to a concurrent grow()
  // operation, but since grow() does not change the content of a bucket we can ignore
  // it and don't have to reread everything during a retry.
retry:
  std::uint32_t item_count = state.item_count();
  for (std::uint32_t i = 0; i != item_count; ++i) {
    if (key == bucket.key[i].load(std::memory_order_relaxed)) {
      // use acquire semantic here - should synchronize-with the release store to value
      // in remove() to ensure that if we see the changed value here we also see the
      // changed state in the subsequent reload of state
      Value v = bucket.value[i].load(std::memory_order_acquire);

      // ensure that we can use the value we just read
      const auto state2 = bucket.state.load(std::memory_order_relaxed);
      if (state.version() != state2.version()) {
        // a deletion has occured in the meantime -> we have to retry
        state = state2;
        goto retry;
      }

      const auto delete_marker = i + 1;
      if (state2.delete_marker() == delete_marker) {
        // Some other thread is currently deleting the entry at this index.
        // The key we just read can be either the original one (i.e., the key that is
        // currently beeing deleted), or another key that is beeing moved to this slot
        // to replace the deleted entry.
        // Unfortunately we cannot differentiate between these two cases, so we simply
        // ignore this item and continue with our search. If we can finish our search
        // before the deleting thread can finish moving some key/value pair into this
        // slot everything is fine. If the delete operation finished before our search,
        // we will recognize this by an updated state and retry.
        continue;
      }

      value = v;
      return true;
    }
  }

  extension_item* extension = bucket.head.load(std::memory_order_acquire);
  while (extension) {
    if (key == extension->key.load(std::memory_order_relaxed)) {
      Value v = extension->value.load(std::memory_order_acquire);

      auto state2 = bucket.state.load(std::memory_order_relaxed);
      if (state.version() != state2.version()) {
        // a deletion has occured in the meantime -> we have to retry
        state = state2;
        goto retry;
      }
      value = v;
      return true;
    }
    
    extension = extension->next.load(std::memory_order_acquire);
    auto state2 = bucket.state.load(std::memory_order_relaxed);
    if (state.version() != state2.version()) {
      // a deletion has occured in the meantime -> we have to retry
      state = state2;
      goto retry;
    }
  }

  auto state2 = bucket.state.load(std::memory_order_relaxed);
  if (state.version() != state2.version()) {
    state = state2;
    // a deletion has occured -> we have to retry since the entry we are looking for might
    // have been moved while we were searching
    goto retry;
  }

  return false;
}

template <class Key, class Value, class... Policies>
void vyukov_hash_map<Key, Value, Policies...>::grow(bucket& bucket, bucket_state state)
{
  // try to acquire the resizeLock
  const int already_resizing = resize_lock.exchange(1, std::memory_order_relaxed);

  // release the bucket lock
  bucket.state.store(state, std::memory_order_relaxed);
  if (already_resizing) {
    backoff backoff;
    // another thread is already resizing -> wait for it to finish
    while (resize_lock.load(std::memory_order_acquire) != 0)
      backoff();

    return;
  }

  // Note: since we hold the resize lock, nobody can replace the current block
  auto b = data_block.load(std::memory_order_acquire);
  const auto bucket_count = b->bucket_count;
  block* new_block = allocate_block(bucket_count * 2);
  if (new_block == nullptr) {
    resize_lock.store(0, std::memory_order_relaxed);
    throw std::bad_alloc();
  }

  // lock all buckets
  auto buckets = b->buckets();
  for (std::uint32_t i = 0; i != bucket_count; ++i) {
    auto& bucket = buckets[i];
    backoff backoff;
    for (;;) {
      auto st = bucket.state.load(std::memory_order_acquire);
      if (st.is_locked())
        continue;
    
      if (bucket.state.compare_exchange_weak(st, st.locked(), std::memory_order_acquire, std::memory_order_relaxed))
        break; // we've got the lock

      backoff();
    }
  }

  auto new_buckets = new_block->buckets();
  for (std::uint32_t bucket_idx = 0; bucket_idx != bucket_count; ++bucket_idx) {
    auto& bucket = buckets[bucket_idx];
    const std::uint32_t item_count = bucket.state.load(std::memory_order_relaxed).item_count();
    for (std::uint32_t i = 0; i != item_count; ++i) {
      Key k = bucket.key[i].load(std::memory_order_relaxed);
      hash_t h = hash{}(k);
      auto& new_bucket = new_buckets[h & new_block->mask];
      auto state = new_bucket.state.load(std::memory_order_relaxed);
      auto new_bucket_count = state.item_count();
      Value v = bucket.value[i].load(std::memory_order_relaxed);
      new_bucket.key[new_bucket_count].store(k, std::memory_order_relaxed);
      new_bucket.value[new_bucket_count].store(v, std::memory_order_relaxed);
      new_bucket.state.store(state.inc_item_count(), std::memory_order_relaxed);
    }

    // relaxed ordering is fine since we own the bucket lock
    for (extension_item* extension = bucket.head;
        extension != nullptr;
        extension = extension->next.load(std::memory_order_relaxed))
    {
      Key k = extension->key.load(std::memory_order_relaxed);
      hash_t h = hash{}(k);
      auto& new_bucket = new_buckets[h & new_block->mask];
      auto state = new_bucket.state.load(std::memory_order_relaxed);
      auto new_bucket_count = state.item_count();
      Value v = extension->value.load(std::memory_order_relaxed);
      if (new_bucket_count < bucket_item_count) {
        new_bucket.key[new_bucket_count].store(k, std::memory_order_relaxed);
        new_bucket.value[new_bucket_count].store(v, std::memory_order_relaxed);
        new_bucket.state.store(state.inc_item_count(), std::memory_order_relaxed);
      } else {
        extension_item* new_extension = allocate_extension_item(new_block, h);
        assert(new_extension);
        new_extension->key.store(k, std::memory_order_relaxed);
        new_extension->value.store(v, std::memory_order_relaxed);
        new_extension->next.store(new_bucket.head, std::memory_order_relaxed);
        new_bucket.head = new_extension;
      }
    }
  }
  data_block.store(new_block, std::memory_order_release);
  resize_lock.store(0, std::memory_order_release);
  
  // reclaim the old data block
  guard_ptr g(b);
  g.reclaim();
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::allocate_block(std::uint32_t bucket_count) -> block* {
  std::uint32_t extension_bucket_count = bucket_count / bucket_to_extension_ratio;
  std::size_t size = sizeof(block) +
    sizeof(bucket) * bucket_count +
    sizeof(extension_bucket) * (extension_bucket_count + 1);

  static constexpr std::uint32_t cacheline_size = 64;
  void* mem = boost::alignment::aligned_alloc(cacheline_size, size);
  if (mem == nullptr)
    return nullptr;
  std::memset(mem, 0, size);
  block* b = new (mem) block;
  b->mask = bucket_count - 1;
  b->bucket_count = bucket_count;
  b->extension_bucket_count = extension_bucket_count;
  // TODO - rewrite casts
  b->extension_buckets = (extension_bucket*)((char*)b + sizeof(block) + sizeof(bucket) * bucket_count);
  if ((uintptr_t)b->extension_buckets % sizeof(extension_bucket))
    // TODO - rewrite casts
    (char*&)b->extension_buckets += sizeof(extension_bucket) - ((uintptr_t)b->extension_buckets % sizeof(extension_bucket));
  
  for (std::uint32_t i = 0; i != extension_bucket_count; ++i) {
    auto& bucket = b->extension_buckets[i];
    extension_item* head = nullptr;
    for (std::uint32_t j = 0; j != extension_item_count; ++j) {
      bucket.items[j].next.store(head, std::memory_order_relaxed);
      head = &bucket.items[j];
    }
    bucket.head = head;
  }

  return b;
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::lock_bucket(hash_t hash, guard_ptr& block, bucket_state& state)
  -> bucket&
{
  backoff backoff;
  for (;;) {
    block.acquire(data_block, std::memory_order_acquire);
    const std::size_t bucket_idx = hash & block->mask;
    auto& bucket = block->buckets()[bucket_idx];
    bucket_state st = bucket.state.load(std::memory_order_relaxed);
    if (st.is_locked())
      continue;
    
    if (bucket.state.compare_exchange_strong(st, st.locked(), std::memory_order_acquire,
                                                              std::memory_order_relaxed)) {
      state = st;
      return bucket;
    }
    backoff();
  }
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::allocate_extension_item(block* b, hash_t hash)
  -> extension_item*
{
  const std::size_t extension_bucket_count = b->extension_bucket_count;
  const std::size_t mod_mask = extension_bucket_count - 1;
  for (std::size_t iter = 0; iter != 2; ++iter) {
    for (std::size_t idx = 0; idx != extension_bucket_count; ++idx) {
      const std::size_t extension_bucket_idx = (hash + idx) & mod_mask;
      extension_bucket& extension_bucket = b->extension_buckets[extension_bucket_idx];

      if (extension_bucket.head == nullptr)
        continue;

      extension_bucket.acquire_lock();
      auto item = extension_bucket.head;
      if (item) {
        extension_bucket.head = item->next;
        extension_bucket.release_lock();
        return item;
      }
      extension_bucket.release_lock();
    }
  }
  return nullptr;
}

template <class Key, class Value, class... Policies>
void vyukov_hash_map<Key, Value, Policies...>::free_extension_item(extension_item* item) {
  // TODO - rewrite casts
  extension_bucket& bucket = *(extension_bucket*)((intptr_t)item - (intptr_t)item % sizeof(extension_bucket));

  bucket.acquire_lock();

  auto head = bucket.head;
  item->next.store(head, std::memory_order_release);
  bucket.head = item;
  bucket.release_lock();
}

}
