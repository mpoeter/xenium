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
  typename traits::storage_key_type key[bucket_item_count];
  typename traits::storage_value_type value[bucket_item_count];
};

template <class Key, class Value, class... Policies>
struct vyukov_hash_map<Key, Value, Policies...>::extension_item {
  typename traits::storage_key_type key;
  typename traits::storage_value_type value;
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
      // (TODO)
      if (lock.exchange(1, std::memory_order_acquire) == 0)
        return;
      backoff();
    }
  }
  void release_lock() {
    // (TODO)
    lock.store(0, std::memory_order_release);
  }
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
  std::uint32_t index(const key_type& key) const { return static_cast<std::uint32_t>(key & mask); }
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
  // (TODO)
  data_block.store(b, std::memory_order_release);
}

template <class Key, class Value, class... Policies>
vyukov_hash_map<Key, Value, Policies...>::~vyukov_hash_map() {
  // delete all remaining entries - this also reclaims any internally allocated
  // nodes as well as managed_ptr instances.
  // This could be implemented in a more efficient way, but it works for now!
  auto it = begin();
  while (it != end())
    erase(it);
  delete data_block.load().get();
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::emplace(key_type key, value_type value) {
  return do_get_or_emplace<false>(
    std::move(key),
    [v = std::move(value)]{ return std::move(v); },
    [](accessor&&, auto&) {});
}

template <class Key, class Value, class... Policies>
template <class... Args>
auto vyukov_hash_map<Key, Value, Policies...>::get_or_emplace(key_type key, Args&&... args)
  -> std::pair<accessor, bool>
{
  std::pair<accessor, bool> result;
  result.second = do_get_or_emplace<true>(
    std::move(key),
    [&]{ return value_type(std::forward<Args>(args)...); },
    [&result](accessor&& acc, auto& cell) {
      result.first = std::move(acc);
    });
  return result;
}

template <class Key, class Value, class... Policies>
template <class Factory>
auto vyukov_hash_map<Key, Value, Policies...>::get_or_emplace_lazy(key_type key, Factory&& factory)
  -> std::pair<accessor, bool>
{
  std::pair<accessor, bool> result;
  result.second = do_get_or_emplace<true>(
    std::move(key),
    std::forward<Factory>(factory),
    [&result](accessor&& acc, auto& cell) {
      result.first = std::move(acc);
    });
  return result;
}

template <class Key, class Value, class... Policies>
template <bool AcquireAccessor, class Factory, class Callback>
bool vyukov_hash_map<Key, Value, Policies...>::do_get_or_emplace(Key&& key, Factory&& factory, Callback&& callback) {
  const hash_t h = hash{}(key);

  accessor acc;
retry:
  guarded_block b;
  bucket_state state;
  bucket& bucket = lock_bucket(h, b, state);
  // TODO - unlock bucket in case of exception!
  std::uint32_t item_count = state.item_count();

  for (std::uint32_t i = 0; i != item_count; ++i)
    if (traits::template compare_key<AcquireAccessor>(bucket.key[i], bucket.value[i], key, h, acc)) {
      callback(std::move(acc), bucket.value[i]);
      bucket.state.store(state, std::memory_order_relaxed);
      return false;
    }

  if (item_count < bucket_item_count) {
    traits::template store_item<AcquireAccessor>(bucket.key[item_count], bucket.value[item_count], h,
      std::move(key), factory(), std::memory_order_relaxed, acc);
    callback(std::move(acc), bucket.value[item_count]);
    // release the bucket lock and increment the item count
    // (TODO)
    bucket.state.store(state.inc_item_count(), std::memory_order_release);
    return true;
  }

  // TODO - keep extension items sorted
  for (extension_item* extension = bucket.head.load(std::memory_order_relaxed);
       extension != nullptr;
       extension = extension->next.load(std::memory_order_relaxed)) {
    if (traits::template compare_key<AcquireAccessor>(extension->key, extension->value, key, h, acc)) {
      callback(std::move(acc), extension->value);
      // release the lock
      bucket.state.store(state, std::memory_order_relaxed);
      return false;
    }
  }

  extension_item* extension = allocate_extension_item(b.get(), h);
  if (extension == nullptr) {
    grow(bucket, state);
    goto retry;
  }
  traits::template store_item<AcquireAccessor>(extension->key, extension->value, h,
    std::move(key), factory(), std::memory_order_relaxed, acc);
  callback(std::move(acc), extension->value);
  // use release semantic here to ensure that a thread in get() that sees the
  // new pointer also sees the new key/value pair
  auto old_head = bucket.head.load(std::memory_order_relaxed);
  // (TODO) - need release here? probably not.
  extension->next.store(old_head, std::memory_order_release);
  // (TODO)
  bucket.head.store(extension, std::memory_order_release);
  // release the bucket lock
  // (TODO)
  bucket.state.store(state, std::memory_order_release);

  return true;
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::erase(const key_type& key) {
  accessor acc;
  bool result = do_extract(key, acc);
  traits::reclaim(acc);
  return result;
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::extract(const key_type& key, accessor& acc) {
  bool result = do_extract(key, acc);
  traits::reclaim_internal(acc);
  return result;
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::do_extract(const key_type& key, accessor& result) {
  const hash_t h = hash{}(key);
  backoff backoff;
  guarded_block b;

restart:
  // (TODO)
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
  // (TODO)
  if (!bucket.state.compare_exchange_strong(state, locked_state, std::memory_order_acquire,
                                                                 std::memory_order_relaxed)) {
    backoff();
    goto restart;
  }

  // TODO - unlock in case of exception! (may be thrown by guard_ptr constructor)
  // we have the lock - now look for the key

  for (std::uint32_t i = 0; i != item_count; ++i) {
    if (traits::template compare_key<true>(bucket.key[i], bucket.value[i], key, h, result)) {
      extension_item* extension = bucket.head.load(std::memory_order_relaxed);
      if (extension) {
        // signal which item we are deleting
        bucket.state.store(locked_state.set_delete_marker(i + 1), std::memory_order_relaxed);

        auto k = extension->key.load(std::memory_order_relaxed);
        auto v = extension->value.load(std::memory_order_relaxed);
        bucket.key[i].store(k, std::memory_order_relaxed);
        // (TODO)
        bucket.value[i].store(v, std::memory_order_release);

        // reset the delete marker
        locked_state = locked_state.new_version();
        // (TODO)
        bucket.state.store(locked_state, std::memory_order_release);

        extension_item* extension_next = extension->next.load(std::memory_order_relaxed);
        // (TODO)
        bucket.head.store(extension_next, std::memory_order_release);

        // release the bucket lock and increase the version
        // (TODO)
        bucket.state.store(locked_state.new_version().clear_lock(), std::memory_order_release);

        free_extension_item(extension);
      } else {
        if (i != item_count - 1) {
          // signal which item we are deleting
          bucket.state.store(locked_state.set_delete_marker(i + 1), std::memory_order_relaxed);

          auto k = bucket.key[item_count - 1].load(std::memory_order_relaxed);
          auto v = bucket.value[item_count - 1].load(std::memory_order_relaxed);
          bucket.key[i].store(k, std::memory_order_relaxed);
          // (TODO)
          bucket.value[i].store(v, std::memory_order_release);
        }

        // release the bucket lock, reset the delete marker (if it is set), increase the version
        // and decrement the item counter.
        // (TODO)
        bucket.state.store(state.new_version().dec_item_count(), std::memory_order_release);
      }
      return true;
    }
  }

  auto extension_prev = &bucket.head;
  extension_item* extension = extension_prev->load(std::memory_order_relaxed);
  while (extension) {
    if (traits::template compare_key<true>(extension->key, extension->value, key, h, result)) {
      extension_item* extension_next = extension->next.load(std::memory_order_relaxed);
      extension_prev->store(extension_next, std::memory_order_relaxed);

      // release the bucket lock and increase the version
      // (TODO)
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
void vyukov_hash_map<Key, Value, Policies...>::erase(iterator& it) {
  if (it.extension) {
    // the item we are currently looking at is an extension item
    auto next = it.extension->next.load(std::memory_order_relaxed);
    it.prev->store(next, std::memory_order_relaxed);
    auto new_state = it.current_bucket_state.locked().new_version();
    it.current_bucket->state.store(new_state, std::memory_order_release);

    free_extension_item(it.extension);
    it.extension = next;
    if (next == nullptr)
      it.move_to_next_bucket();
    return;
  }

  // the item we are currently looking at is in the bucket array

  auto extension = it.current_bucket->head.load(std::memory_order_relaxed);
  if (extension) {
    auto locked_state = it.current_bucket_state;
    auto marked_state = locked_state.set_delete_marker(it.index + 1);
    it.current_bucket->state.store(marked_state, std::memory_order_relaxed);

    auto k = extension->key.load(std::memory_order_relaxed);
    auto v = extension->value.load(std::memory_order_relaxed);
    it.current_bucket->key[it.index].store(k, std::memory_order_relaxed);
    // (TODO)
    it.current_bucket->value[it.index].store(v, std::memory_order_release);

    // reset the delete marker
    locked_state = locked_state.new_version();
    // (TODO)
    it.current_bucket->state.store(locked_state, std::memory_order_release);

    auto next = extension->next.load(std::memory_order_relaxed);
    // (TODO)
    it.current_bucket->head.store(next, std::memory_order_release);

    // increase the version but keep the lock
    // (TODO)
    it.current_bucket->state.store(locked_state.new_version(), std::memory_order_release);

    free_extension_item(extension);
  } else {
    auto max_index = it.current_bucket_state.item_count() - 1;
    if (it.index != max_index) {
      // signal which item we are deleting
      auto locked_state = it.current_bucket_state;
      auto marked_state = locked_state.set_delete_marker(it.index + 1);
      it.current_bucket->state.store(marked_state, std::memory_order_relaxed);

      auto k = it.current_bucket->key[max_index].load(std::memory_order_relaxed);
      auto v = it.current_bucket->value[max_index].load(std::memory_order_relaxed);
      it.current_bucket->key[it.index].store(k, std::memory_order_relaxed);
      // (TODO)
      it.current_bucket->value[it.index].store(v, std::memory_order_release);
    }

    auto new_state = it.current_bucket_state.new_version().dec_item_count();
    it.current_bucket_state = new_state;
    it.current_bucket->state.store(new_state, std::memory_order_release);
    if (it.index == new_state.item_count()) {
      it.prev = &it.current_bucket->head;
      it.extension = it.current_bucket->head.load(std::memory_order_relaxed);
      if (it.extension == nullptr)
        it.move_to_next_bucket();
    }
  }
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::try_get_value(const key_type& key, accessor& value) const {
  const hash_t h = hash{}(key);

  // (TODO)
  guarded_block b = acquire_guard(data_block, std::memory_order_acquire);
  const std::size_t bucket_idx = h & b->mask;
  bucket& bucket = b->buckets()[bucket_idx];
  // (TODO)
  bucket_state state = bucket.state.load(std::memory_order_acquire);

  // data_block (and therefore the bucket) can only change due to a concurrent grow()
  // operation, but since grow() does not change the content of a bucket we can ignore
  // it and don't have to reread everything during a retry.
retry:
  std::uint32_t item_count = state.item_count();
  for (std::uint32_t i = 0; i != item_count; ++i) {
    if (traits::compare_trivial_key(bucket.key[i], key, h)) {
      // use acquire semantic here - should synchronize-with the release store to value
      // in remove() to ensure that if we see the changed value here we also see the
      // changed state in the subsequent reload of state
      // (TODO)
      accessor acc = traits::acquire(bucket.value[i], std::memory_order_acquire);

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

      if (!traits::compare_nontrivial_key(acc, key))
        continue;

      value = std::move(acc);
      return true;
    }
  }

  // (TODO)
  extension_item* extension = bucket.head.load(std::memory_order_acquire);
  while (extension) {
    if (traits::compare_trivial_key(extension->key, key, h)) {
      // (TODO)
      accessor acc = traits::acquire(extension->value, std::memory_order_acquire);

      auto state2 = bucket.state.load(std::memory_order_relaxed);
      if (state.version() != state2.version()) {
        // a deletion has occured in the meantime -> we have to retry
        state = state2;
        goto retry;
      }

      if (!traits::compare_nontrivial_key(acc, key))
        continue;

      value = std::move(acc);
      return true;
    }

    // (TODO)
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
    // (TODO)
    while (resize_lock.load(std::memory_order_acquire) != 0)
      backoff();

    return;
  }

  // Note: since we hold the resize lock, nobody can replace the current block
  // (TODO)
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
      // (TODO)
      auto st = bucket.state.load(std::memory_order_acquire);
      if (st.is_locked())
        continue;

      // (TODO)
      if (bucket.state.compare_exchange_weak(st, st.locked(),
          std::memory_order_acquire, std::memory_order_relaxed))
        break; // we've got the lock

      backoff();
    }
  }

  auto new_buckets = new_block->buckets();
  for (std::uint32_t bucket_idx = 0; bucket_idx != bucket_count; ++bucket_idx) {
    auto& bucket = buckets[bucket_idx];
    const std::uint32_t item_count = bucket.state.load(std::memory_order_relaxed).item_count();
    for (std::uint32_t i = 0; i != item_count; ++i) {
      auto k = bucket.key[i].load(std::memory_order_relaxed);
      hash_t h = traits::template rehash<hash>(k);
      auto& new_bucket = new_buckets[h & new_block->mask];
      auto state = new_bucket.state.load(std::memory_order_relaxed);
      auto new_bucket_count = state.item_count();
      auto v = bucket.value[i].load(std::memory_order_relaxed);
      new_bucket.key[new_bucket_count].store(k, std::memory_order_relaxed);
      new_bucket.value[new_bucket_count].store(v, std::memory_order_relaxed);
      new_bucket.state.store(state.inc_item_count(), std::memory_order_relaxed);
    }

    // relaxed ordering is fine since we own the bucket lock
    for (extension_item* extension = bucket.head;
        extension != nullptr;
        extension = extension->next.load(std::memory_order_relaxed))
    {
      auto k = extension->key.load(std::memory_order_relaxed);
      hash_t h = traits::template rehash<hash>(k);
      auto& new_bucket = new_buckets[h & new_block->mask];
      auto state = new_bucket.state.load(std::memory_order_relaxed);
      auto new_bucket_count = state.item_count();
      auto v = extension->value.load(std::memory_order_relaxed);
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
  // (TODO)
  data_block.store(new_block, std::memory_order_release);
  // (TODO)
  resize_lock.store(0, std::memory_order_release);

  // reclaim the old data block
  guarded_block g(b);
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

  std::size_t extension_bucket_addr =
    reinterpret_cast<std::size_t>(b) + sizeof(block) + sizeof(bucket) * bucket_count;
  if (extension_bucket_addr % sizeof(extension_bucket) != 0) {
    extension_bucket_addr += sizeof(extension_bucket) - (extension_bucket_addr % sizeof(extension_bucket));
  }
  b->extension_buckets = reinterpret_cast<extension_bucket*>(extension_bucket_addr);

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
auto vyukov_hash_map<Key, Value, Policies...>::begin() -> iterator {
  iterator result;
  result.current_bucket = &lock_bucket(0, result.block, result.current_bucket_state);
  if (result.current_bucket_state.item_count() == 0)
    result.move_to_next_bucket();
  return result;
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::lock_bucket(hash_t hash, guarded_block& block, bucket_state& state)
  -> bucket&
{
  backoff backoff;
  for (;;) {
    // (TODO)
    block.acquire(data_block, std::memory_order_acquire);
    const std::size_t bucket_idx = hash & block->mask;
    auto& bucket = block->buckets()[bucket_idx];
    bucket_state st = bucket.state.load(std::memory_order_relaxed);
    if (st.is_locked())
      continue;

    // (TODO)
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
  std::size_t item_addr = reinterpret_cast<std::size_t>(item);
  auto bucket = reinterpret_cast<extension_bucket*>(item_addr - item_addr % sizeof(extension_bucket));

  bucket->acquire_lock();
  auto head = bucket->head;
  // (TODO)
  item->next.store(head, std::memory_order_release);
  bucket->head = item;
  bucket->release_lock();
}

template <class Key, class Value, class... Policies>
vyukov_hash_map<Key, Value, Policies...>::iterator::iterator() :
  block(),
  current_bucket(),
  current_bucket_state(),
  index(),
  extension(),
  prev()
{}

template <class Key, class Value, class... Policies>
vyukov_hash_map<Key, Value, Policies...>::iterator::~iterator() {
  reset();
}

template <class Key, class Value, class... Policies>
void vyukov_hash_map<Key, Value, Policies...>::iterator::reset() {
  // unlock the current bucket
  if (current_bucket)
    current_bucket->state.store(current_bucket_state, std::memory_order_release);

  block.reset();
  current_bucket = nullptr;
  current_bucket_state = bucket_state();
  index = 0;
  extension = nullptr;
  prev = nullptr;
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::iterator::operator==(const iterator& r) const {
  return block == r.block &&
    current_bucket == r.current_bucket &&
    current_bucket_state == r.current_bucket_state &&
    index == r.index &&
    extension == r.extension;
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::iterator::operator!=(const iterator& r) const {
  return !(*this == r);
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::iterator::operator++() -> iterator& {
  if (extension) {
    prev = &extension->next;
    extension = extension->next.load(std::memory_order_relaxed);
    if (extension == nullptr)
      move_to_next_bucket();
    return *this;
  }

  ++index;
  if (index == current_bucket_state.item_count()) {
    prev = &current_bucket->head;
    extension = current_bucket->head.load(std::memory_order_relaxed);
    if (extension == nullptr)
      move_to_next_bucket();
  }
  return *this;
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::iterator::operator->() -> pointer {
  static_assert(std::is_reference<reference>::value,
    "operator-> is only available for instantiations with non-trivial key types. Use explicit "
    "dereferenciation instead (operator*). The reason is that all other instantiations create "
    "temporary std::pair<> instances since key and value are stored separately.");
  return &this->operator*();
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::iterator::operator*() -> reference {
  if (extension) {
    return traits::deref_iterator(extension->key, extension->value);
  }
  return traits::deref_iterator(current_bucket->key[index], current_bucket->value[index]);
}

template <class Key, class Value, class... Policies>
void vyukov_hash_map<Key, Value, Policies...>::iterator::move_to_next_bucket() {
  assert(current_bucket != nullptr);
  if (current_bucket == block->buckets() + block->bucket_count - 1) {
    // we reached the end of the container -> reset the iterator
    reset();
    return;
  }

  auto old_bucket = current_bucket;
  auto old_bucket_state = current_bucket_state;
  ++current_bucket; // move pointer to the next bucket

  backoff backoff;
  for (;;) {
    auto st = current_bucket->state.load(std::memory_order_acquire);
    if (st.is_locked())
      continue;

    if (current_bucket->state.compare_exchange_strong(st, st.locked(), std::memory_order_acquire,
                                                                       std::memory_order_relaxed))
    {
      current_bucket_state = st;
      break;
    }
    backoff();
  }

  old_bucket->state.store(old_bucket_state, std::memory_order_release); // unlock the previous bucket

  index = 0;
  extension = nullptr;
  prev = nullptr;
  if (current_bucket_state.item_count() == 0)
    move_to_next_bucket();
}

}
