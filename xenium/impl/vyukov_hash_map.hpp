//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_VYUKOV_HASH_MAP_IMPL
  #error "This is an impl file and must not be included directly!"
#endif

#include <xenium/acquire_guard.hpp>
#include <xenium/backoff.hpp>
#include <xenium/parameter.hpp>
#include <xenium/policy.hpp>

#include <atomic>
#include <cassert>
#include <cstring>

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable : 26495) // uninitialized member variable
  #pragma warning(disable : 4324)  // structure was padded due to alignment specifier
#endif

namespace xenium {

template <class Key, class Value, class... Policies>
struct vyukov_hash_map<Key, Value, Policies...>::bucket_state {
  bucket_state() = default;
  
  [[nodiscard]] bucket_state locked() const noexcept { return bucket_state(value | lock); }
  [[nodiscard]] bucket_state clear_lock() const {
    assert(value & lock);
    return bucket_state(value ^ lock);
  }
  [[nodiscard]] bucket_state new_version() const noexcept { return bucket_state(value + version_inc); }
  [[nodiscard]] bucket_state inc_item_count() const {
    assert(item_count() < bucket_item_count);
    bucket_state result(value + item_count_inc);
    assert(result.item_count() == item_count() + 1);
    return result;
  }
  [[nodiscard]] bucket_state dec_item_count() const {
    assert(item_count() > 0);
    bucket_state result(value - item_count_inc);
    assert(result.item_count() == item_count() - 1);
    return result;
  }
  [[nodiscard]] bucket_state set_delete_marker(std::uint32_t marker) const {
    assert(delete_marker() == 0);
    bucket_state result(value | (marker << delete_marker_shift));
    assert(result.delete_marker() == marker);
    return result;
  }

  bool operator==(bucket_state r) const noexcept { return this->value == r.value; }
  bool operator!=(bucket_state r) const noexcept { return this->value != r.value; }

  [[nodiscard]] std::uint32_t item_count() const noexcept { return (value >> 1) & item_count_mask; }
  [[nodiscard]] std::uint32_t delete_marker() const noexcept {
    return (value >> delete_marker_shift) & item_count_mask;
  }
  [[nodiscard]] std::uint32_t version() const noexcept { return value >> version_shift; }
  [[nodiscard]] bool is_locked() const noexcept { return (value & lock) != 0; }

private:
  explicit bucket_state(std::uint32_t value) noexcept : value(value) {}

  std::uint32_t value{};

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
  std::atomic<extension_item*> head;
  extension_item items[extension_item_count];

  void acquire_lock() {
    backoff backoff;
    for (;;) {
      while (lock.load(std::memory_order_relaxed) != 0) {
        ;
      }
      // (1) - this acquire-exchange synchronizes-with the release-store (2)
      if (lock.exchange(1, std::memory_order_acquire) == 0) {
        return;
      }
      backoff();
    }
  }
  void release_lock() {
    // (2) - this release-store synchronizes-with the acquire-exchange (1)
    lock.store(0, std::memory_order_release);
  }
};

template <class Key, class Value, class... Policies>
struct alignas(64) vyukov_hash_map<Key, Value, Policies...>::block : reclaimer::template enable_concurrent_ptr<block> {
  std::uint32_t mask;
  std::uint32_t bucket_count;
  std::uint32_t extension_bucket_count;
  extension_bucket* extension_buckets;

  // TODO - adapt to be customizable via map_to_bucket policy
  [[nodiscard]] std::uint32_t index(const key_type& key) const { return static_cast<std::uint32_t>(key & mask); }
  bucket* buckets() { return reinterpret_cast<bucket*>(this + 1); }

  void operator delete(void* p) { ::operator delete(p, cacheline_size); } // NOLINT (new-delete-overloads)
};

template <class Key, class Value, class... Policies>
struct vyukov_hash_map<Key, Value, Policies...>::unlocker {
  unlocker(bucket& locked_bucket, bucket_state state) : state(state), locked_bucket(locked_bucket) {}
  ~unlocker() {
    if (enabled) {
      assert(locked_bucket.state.load().is_locked());
      locked_bucket.state.store(state, std::memory_order_relaxed);
    }
  }
  void unlock(bucket_state new_state, std::memory_order order) {
    assert(enabled);
    assert(locked_bucket.state.load().is_locked());
    locked_bucket.state.store(new_state, order);
    enabled = false;
  }
  void disable() { enabled = false; }

private:
  bool enabled = true;
  bucket_state state;
  bucket& locked_bucket;
};

template <class Key, class Value, class... Policies>
vyukov_hash_map<Key, Value, Policies...>::vyukov_hash_map(std::size_t initial_capacity) : resize_lock(0) {
  auto b = allocate_block(static_cast<std::uint32_t>(utils::next_power_of_two(initial_capacity)));
  if (b == nullptr) {
    throw std::bad_alloc();
  }
  data_block.store(b, std::memory_order_relaxed);
}

template <class Key, class Value, class... Policies>
vyukov_hash_map<Key, Value, Policies...>::~vyukov_hash_map() {
  // delete all remaining entries - this also reclaims any internally allocated
  // nodes as well as managed_ptr instances.
  // This could be implemented in a more efficient way, but it works for now!
  try {
    auto it = begin();
    while (it != end()) {
      try {
        erase(it);
      } catch(...) {
      }
    }
  } catch(...) {
  }
  delete data_block.load().get();
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::emplace(key_type key, value_type value) {
  return do_get_or_emplace<false>(
    std::move(key), [v = std::move(value)]() mutable { return std::move(v); }, [](accessor&&, auto&) {});
}

template <class Key, class Value, class... Policies>
template <class... Args>
auto vyukov_hash_map<Key, Value, Policies...>::get_or_emplace(key_type key, Args&&... args)
  -> std::pair<accessor, bool> {
  std::pair<accessor, bool> result;
  result.second = do_get_or_emplace<true>(
    std::move(key),
    [&] { return value_type(std::forward<Args>(args)...); },
    [&result](accessor&& acc, auto&) { result.first = std::move(acc); });
  return result;
}

template <class Key, class Value, class... Policies>
template <class Factory>
auto vyukov_hash_map<Key, Value, Policies...>::get_or_emplace_lazy(key_type key, Factory&& factory)
  -> std::pair<accessor, bool> {
  std::pair<accessor, bool> result;
  result.second = do_get_or_emplace<true>(std::move(key),
                                          std::forward<Factory>(factory),
                                          [&result](accessor&& acc, auto&) { result.first = std::move(acc); });
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

  unlocker unlocker(bucket, state);

  std::uint32_t item_count = state.item_count();

  for (std::uint32_t i = 0; i != item_count; ++i) {
    if (traits::template compare_key<AcquireAccessor>(bucket.key[i], bucket.value[i], key, h, acc)) {
      callback(std::move(acc), bucket.value[i]);
      unlocker.unlock(state, std::memory_order_relaxed);
      return false;
    }
  }

  if (item_count < bucket_item_count) {
    traits::template store_item<AcquireAccessor>(
      bucket.key[item_count], bucket.value[item_count], h, std::move(key), factory(), std::memory_order_relaxed, acc);
    callback(std::move(acc), bucket.value[item_count]);
    // release the bucket lock and increment the item count
    // (3) - this release-store synchronizes-with the acquire-CAS (7, 30, 34, 37) and the acquire-load (23)
    unlocker.unlock(state.inc_item_count(), std::memory_order_release);
    return true;
  }

  // TODO - keep extension items sorted
  for (extension_item* extension = bucket.head.load(std::memory_order_relaxed); extension != nullptr;
       extension = extension->next.load(std::memory_order_relaxed)) {
    if (traits::template compare_key<AcquireAccessor>(extension->key, extension->value, key, h, acc)) {
      callback(std::move(acc), extension->value);
      unlocker.unlock(state, std::memory_order_relaxed);
      return false;
    }
  }

  extension_item* extension = allocate_extension_item(b.get(), h);
  if (extension == nullptr) {
    unlocker.disable(); // bucket is unlocked in grow()
    grow(bucket, state);
    goto retry;
  }
  try {
    traits::template store_item<AcquireAccessor>(
      extension->key, extension->value, h, std::move(key), factory(), std::memory_order_relaxed, acc);
  } catch (...) {
    free_extension_item(extension);
    throw;
  }
  callback(std::move(acc), extension->value);
  auto old_head = bucket.head.load(std::memory_order_relaxed);
  extension->next.store(old_head, std::memory_order_relaxed);
  // (4) - this release-store synchronizes-with the acquire-load (25)
  bucket.head.store(extension, std::memory_order_release);
  // release the bucket lock
  // (5) - this release-store synchronizes-with the acquire-CAS (7, 30, 34, 37) and the acquire-load (23)
  unlocker.unlock(state, std::memory_order_release);

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
  // (6) - this acquire-load synchronizes-with the release-store (31)
  b.acquire(data_block, std::memory_order_acquire);
  const std::size_t bucket_idx = h & b->mask;
  bucket& bucket = b->buckets()[bucket_idx];
  bucket_state state = bucket.state.load(std::memory_order_relaxed);
  std::uint32_t item_count = state.item_count();

  if (item_count == 0) {
    return false; // no items to check
  }

  if (state.is_locked()) {
    goto restart;
  }

  auto locked_state = state.locked();
  // (7) - this acquire-CAS synchronizes-with the release-store (3, 5, 11, 13, 14, 36, 38)
  if (!bucket.state.compare_exchange_strong(
        state, locked_state, std::memory_order_acquire, std::memory_order_relaxed)) {
    backoff();
    goto restart;
  }

  unlocker unlocker(bucket, state);

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
        // (8)  - this release-store synchronizes-with the acquire-load (24)
        bucket.value[i].store(v, std::memory_order_release);

        // reset the delete marker
        locked_state = locked_state.new_version();
        // (9) - this release-store synchronizes-with the acquire-load (23)
        bucket.state.store(locked_state, std::memory_order_release);

        extension_item* extension_next = extension->next.load(std::memory_order_relaxed);
        // (10) - this release-store synchronizes-with the acquire-load (25)
        bucket.head.store(extension_next, std::memory_order_release);

        // release the bucket lock and increase the version
        // (11) - this release-store synchronizes-with the acquire-CAS (7, 30, 34, 37) and the acquire-load (23)
        unlocker.unlock(locked_state.new_version().clear_lock(), std::memory_order_release);

        free_extension_item(extension);
      } else {
        if (i != item_count - 1) {
          // signal which item we are deleting
          bucket.state.store(locked_state.set_delete_marker(i + 1), std::memory_order_relaxed);

          auto k = bucket.key[item_count - 1].load(std::memory_order_relaxed);
          auto v = bucket.value[item_count - 1].load(std::memory_order_relaxed);
          bucket.key[i].store(k, std::memory_order_relaxed);
          // (12) - this release-store synchronizes-with the acquire-load (24)
          bucket.value[i].store(v, std::memory_order_release);
        }

        // release the bucket lock, reset the delete marker (if it is set), increase the version
        // and decrement the item counter.
        // (13) - this release-store synchronizes-with the acquire-CAS (7, 30, 34, 37) and the acquire-load (23)
        unlocker.unlock(state.new_version().dec_item_count(), std::memory_order_release);
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
      // (14) - this release-store synchronizes-with the acquire-CAS (7, 30, 34, 37) and the acquire-load (23)
      unlocker.unlock(state.new_version(), std::memory_order_release);

      free_extension_item(extension);
      return true;
    }
    extension_prev = &extension->next;
    extension = extension_prev->load(std::memory_order_relaxed);
  }

  // key not found

  // release the bucket lock
  unlocker.unlock(state, std::memory_order_relaxed);

  return false;
}

template <class Key, class Value, class... Policies>
void vyukov_hash_map<Key, Value, Policies...>::erase(iterator& pos) {
  if (pos.extension) {
    // the item we are currently looking at is an extension item
    auto next = pos.extension->next.load(std::memory_order_relaxed);
    pos.prev->store(next, std::memory_order_relaxed);
    auto new_state = pos.current_bucket_state.locked().new_version();
    // (15) - this release-store synchronizes-with the acquire-load (23)
    pos.current_bucket->state.store(new_state, std::memory_order_release);

    free_extension_item(pos.extension);
    pos.extension = next;
    if (next == nullptr) {
      pos.move_to_next_bucket();
    }
    return;
  }

  // the item we are currently looking at is in the bucket array

  auto extension = pos.current_bucket->head.load(std::memory_order_relaxed);
  if (extension) {
    auto locked_state = pos.current_bucket_state.locked();
    auto marked_state = locked_state.set_delete_marker(pos.index + 1);
    pos.current_bucket->state.store(marked_state, std::memory_order_relaxed);
    assert(pos.current_bucket->state.load().is_locked());

    auto k = extension->key.load(std::memory_order_relaxed);
    auto v = extension->value.load(std::memory_order_relaxed);
    pos.current_bucket->key[pos.index].store(k, std::memory_order_relaxed);
    // (16) - this release-store synchronizes-with the acquire-load (24)
    pos.current_bucket->value[pos.index].store(v, std::memory_order_release);

    // reset the delete marker
    locked_state = locked_state.new_version();
    // (17) - this release-store synchronizes-with the acquire-load (23)
    pos.current_bucket->state.store(locked_state, std::memory_order_release);
    assert(pos.current_bucket->state.load().is_locked());

    auto next = extension->next.load(std::memory_order_relaxed);
    // (18) - this release-store synchronizes-with the acquire-load (25)
    pos.current_bucket->head.store(next, std::memory_order_release);

    // increase the version but keep the lock
    // (19) - this release-store synchronizes-with the acquire-load (23)
    pos.current_bucket->state.store(locked_state.new_version(), std::memory_order_release);
    assert(pos.current_bucket->state.load().is_locked());
    free_extension_item(extension);
  } else {
    auto max_index = pos.current_bucket_state.item_count() - 1;
    if (pos.index != max_index) {
      // signal which item we are deleting
      auto locked_state = pos.current_bucket_state.locked();
      auto marked_state = locked_state.set_delete_marker(pos.index + 1);
      pos.current_bucket->state.store(marked_state, std::memory_order_relaxed);
      assert(pos.current_bucket->state.load().is_locked());

      auto k = pos.current_bucket->key[max_index].load(std::memory_order_relaxed);
      auto v = pos.current_bucket->value[max_index].load(std::memory_order_relaxed);
      pos.current_bucket->key[pos.index].store(k, std::memory_order_relaxed);
      // (20) - this release-store synchronizes-with the acquire-load  (24)
      pos.current_bucket->value[pos.index].store(v, std::memory_order_release);
    }

    auto new_state = pos.current_bucket_state.new_version().dec_item_count();
    pos.current_bucket_state = new_state;

    // (21) - this release store synchronizes-with the acquire-load (23)
    pos.current_bucket->state.store(new_state.locked(), std::memory_order_release);
    assert(pos.current_bucket->state.load().is_locked());
    if (pos.index == new_state.item_count()) {
      pos.prev = &pos.current_bucket->head;
      pos.extension = pos.current_bucket->head.load(std::memory_order_relaxed);
      if (pos.extension == nullptr) {
        pos.move_to_next_bucket();
      }
    }
  }
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::try_get_value(const key_type& key, accessor& result) const {
  const hash_t h = hash{}(key);

  // (22) - this acquire-load synchronizes-with the release-store (31)
  guarded_block b = acquire_guard(data_block, std::memory_order_acquire);
  const std::size_t bucket_idx = h & b->mask;
  bucket& bucket = b->buckets()[bucket_idx];

retry:
  // data_block (and therefore the bucket) can only change due to a concurrent grow()
  // operation, but since grow() does not change the content of a bucket we can ignore
  // it and don't have to reread everything during a retry.

  // (23) - this acquire-load synchronizes-with the release-store (3, 5, 9, 11, 13, 14, 15, 17, 19, 21, 36, 38)
  bucket_state state = bucket.state.load(std::memory_order_acquire);

  std::uint32_t item_count = state.item_count();
  for (std::uint32_t i = 0; i != item_count; ++i) {
    if (traits::compare_trivial_key(bucket.key[i], key, h)) {
      // use acquire semantic here - should synchronize-with the release store to value
      // in remove() to ensure that if we see the changed value here we also see the
      // changed state in the subsequent reload of state
      // (24) - this acquire-load synchronizes-with the release-store (8, 12, 16, 20)
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

      if (!traits::compare_nontrivial_key(acc, key)) {
        continue;
      }

      result = std::move(acc);
      return true;
    }
  }

  // (25) - this acquire-load synchronizes-with the release-store (4, 10, 18)
  extension_item* extension = bucket.head.load(std::memory_order_acquire);
  while (extension) {
    if (traits::compare_trivial_key(extension->key, key, h)) {
      // TODO - this acquire does not synchronize with anything ATM.
      // However, this is probably required when introducing an update-method that
      // allows to store a new value.
      // (26) - this acquire-load synchronizes-with <nothing>
      accessor acc = traits::acquire(extension->value, std::memory_order_acquire);

      auto state2 = bucket.state.load(std::memory_order_relaxed);
      if (state.version() != state2.version()) {
        // a deletion has occured in the meantime -> we have to retry
        state = state2;
        goto retry;
      }

      if (!traits::compare_nontrivial_key(acc, key)) {
        continue;
      }

      result = std::move(acc);
      return true;
    }

    // (27) - this acquire-load synchronizes-with the release-store (35)
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
void vyukov_hash_map<Key, Value, Policies...>::grow(bucket& bucket, bucket_state state) {
  // try to acquire the resizeLock
  const int already_resizing = resize_lock.exchange(1, std::memory_order_relaxed);

  // release the bucket lock
  bucket.state.store(state, std::memory_order_relaxed);

  // we intentionally release the bucket lock only after we tried to acquire
  // the resize_lock, to avoid the situation where we might miss a resize
  // performed by some other thread.

  if (already_resizing != 0) {
    backoff backoff;
    // another thread is already resizing -> wait for it to finish
    // (28) - this acquire-load synchronizes-with the release-store (32)
    while (resize_lock.load(std::memory_order_acquire) != 0) {
      backoff();
    }

    return;
  }

  do_grow();
}

template <class Key, class Value, class... Policies>
void vyukov_hash_map<Key, Value, Policies...>::do_grow() {
  // Note: since we hold the resize lock, nobody can replace the current block
  // (29) - this acquire-load synchronizes-with the release-store (31)
  auto old_block = data_block.load(std::memory_order_acquire);
  const auto bucket_count = old_block->bucket_count;
  block* new_block = allocate_block(bucket_count * 2);
  if (new_block == nullptr) {
    resize_lock.store(0, std::memory_order_relaxed);
    throw std::bad_alloc();
  }

  // lock all buckets
  auto old_buckets = old_block->buckets();
  for (std::uint32_t i = 0; i != bucket_count; ++i) {
    auto& bucket = old_buckets[i];
    backoff backoff;
    for (;;) {
      auto st = bucket.state.load(std::memory_order_relaxed);
      if (st.is_locked()) {
        continue;
      }

      // (30) - this acquire-CAS synchronizes-with the release-store (3, 5, 11, 13, 14, 36, 38)
      if (bucket.state.compare_exchange_strong(st, st.locked(), std::memory_order_acquire, std::memory_order_relaxed)) {
        break; // we've got the lock
      }

      backoff();
    }
  }

  auto new_buckets = new_block->buckets();
  for (std::uint32_t bucket_idx = 0; bucket_idx != bucket_count; ++bucket_idx) {
    auto& old_bucket = old_buckets[bucket_idx];
    const std::uint32_t item_count = old_bucket.state.load(std::memory_order_relaxed).item_count();
    for (std::uint32_t i = 0; i != item_count; ++i) {
      auto k = old_bucket.key[i].load(std::memory_order_relaxed);
      hash_t h = traits::template rehash<hash>(k);
      auto& new_bucket = new_buckets[h & new_block->mask];
      auto new_bucket_state = new_bucket.state.load(std::memory_order_relaxed);
      auto new_bucket_count = new_bucket_state.item_count();
      auto v = old_bucket.value[i].load(std::memory_order_relaxed);
      new_bucket.key[new_bucket_count].store(k, std::memory_order_relaxed);
      new_bucket.value[new_bucket_count].store(v, std::memory_order_relaxed);
      new_bucket.state.store(new_bucket_state.inc_item_count(), std::memory_order_relaxed);
    }

    // relaxed ordering is fine since we own the bucket lock
    for (extension_item* extension = old_bucket.head; extension != nullptr;
         extension = extension->next.load(std::memory_order_relaxed)) {
      auto k = extension->key.load(std::memory_order_relaxed);
      hash_t h = traits::template rehash<hash>(k);
      auto& new_bucket = new_buckets[h & new_block->mask];
      auto new_bucket_state = new_bucket.state.load(std::memory_order_relaxed);
      auto new_bucket_count = new_bucket_state.item_count();
      auto v = extension->value.load(std::memory_order_relaxed);
      if (new_bucket_count < bucket_item_count) {
        new_bucket.key[new_bucket_count].store(k, std::memory_order_relaxed);
        new_bucket.value[new_bucket_count].store(v, std::memory_order_relaxed);
        new_bucket.state.store(new_bucket_state.inc_item_count(), std::memory_order_relaxed);
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
  // (31) - this release-store synchronizes-with (6, 22, 29, 33)
  data_block.store(new_block, std::memory_order_release);
  // (32) - this release-store synchronizes-with the acquire-load (28)
  resize_lock.store(0, std::memory_order_release);

  // reclaim the old data block
  guarded_block g(old_block);
  g.reclaim();
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::allocate_block(std::uint32_t bucket_count) -> block* {
  std::uint32_t extension_bucket_count = bucket_count / bucket_to_extension_ratio;
  std::size_t size = sizeof(block) + sizeof(bucket) * bucket_count +
                     sizeof(extension_bucket) * (static_cast<size_t>(extension_bucket_count) + 1);

  void* mem = ::operator new(size, cacheline_size, std::nothrow);
  if (mem == nullptr) {
    return nullptr;
  }

  std::memset(mem, 0, size);
  auto* b = new (mem) block;
  b->mask = bucket_count - 1;
  b->bucket_count = bucket_count;
  b->extension_bucket_count = extension_bucket_count;

  std::size_t extension_bucket_addr = reinterpret_cast<std::size_t>(b) + sizeof(block) + sizeof(bucket) * bucket_count;
  if (extension_bucket_addr % sizeof(extension_bucket) != 0) {
    extension_bucket_addr += sizeof(extension_bucket) - (extension_bucket_addr % sizeof(extension_bucket));
  }
  b->extension_buckets = reinterpret_cast<extension_bucket*>(extension_bucket_addr);

  for (std::uint32_t i = 0; i != extension_bucket_count; ++i) {
    auto& bucket = b->extension_buckets[i];
    extension_item* head = nullptr;
    for (std::size_t j = 0; j != extension_item_count; ++j) {
      bucket.items[j].next.store(head, std::memory_order_relaxed);
      head = &bucket.items[j];
    }
    bucket.head.store(head, std::memory_order_relaxed);
  }

  return b;
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::find(const key_type& key) -> iterator {
  const auto h = hash{}(key);
  iterator result;

  result.current_bucket = &lock_bucket(h, result.block, result.current_bucket_state);
  auto& bucket = *result.current_bucket;

  accessor acc;
  auto item_count = result.current_bucket_state.item_count();
  for (std::uint32_t i = 0; i != item_count; ++i) {
    if (traits::template compare_key<false>(bucket.key[i], bucket.value[i], key, h, acc)) {
      result.index = i;
      return result;
    }
  }

  auto extension = bucket.head.load(std::memory_order_relaxed);
  while (extension) {
    if (traits::template compare_key<false>(extension->key, extension->value, key, h, acc)) {
      result.extension = extension;
      return result;
    }
    extension = extension->next.load(std::memory_order_relaxed);
  }

  return end();
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::begin() -> iterator {
  iterator result;
  result.current_bucket = &lock_bucket(0, result.block, result.current_bucket_state);
  if (result.current_bucket_state.item_count() == 0) {
    result.move_to_next_bucket();
  }
  return result;
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::lock_bucket(hash_t hash, guarded_block& block, bucket_state& state)
  -> bucket& {
  backoff backoff;
  for (;;) {
    // (33) - this acquire-load synchronizes-with the release-store (31)
    block.acquire(data_block, std::memory_order_acquire);
    const std::size_t bucket_idx = hash & block->mask;
    auto& bucket = block->buckets()[bucket_idx];
    bucket_state st = bucket.state.load(std::memory_order_relaxed);
    if (st.is_locked()) {
      continue;
    }

    // (34) - this acquire-CAS synchronizes-with the release-store (3, 5, 11, 13, 14, 36, 38)
    if (bucket.state.compare_exchange_strong(st, st.locked(), std::memory_order_acquire, std::memory_order_relaxed)) {
      state = st;
      return bucket;
    }
    backoff();
  }
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::allocate_extension_item(block* b, hash_t hash) -> extension_item* {
  const std::size_t extension_bucket_count = b->extension_bucket_count;
  const std::size_t mod_mask = extension_bucket_count - 1;
  for (std::size_t iter = 0; iter != 2; ++iter) {
    for (std::size_t idx = 0; idx != extension_bucket_count; ++idx) {
      const std::size_t extension_bucket_idx = (hash + idx) & mod_mask;
      extension_bucket& extension_bucket = b->extension_buckets[extension_bucket_idx];

      if (extension_bucket.head.load(std::memory_order_relaxed) == nullptr) {
        continue;
      }

      extension_bucket.acquire_lock();
      auto item = extension_bucket.head.load(std::memory_order_relaxed);
      if (item) {
        extension_bucket.head.store(item->next, std::memory_order_relaxed);
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
  auto item_addr = reinterpret_cast<std::uintptr_t>(item);
  auto bucket = reinterpret_cast<extension_bucket*>(item_addr - item_addr % sizeof(extension_bucket));

  bucket->acquire_lock();
  auto head = bucket->head.load(std::memory_order_relaxed);
  // (35) - this release-store synchronizes-with the acquire-load (27)
  item->next.store(head, std::memory_order_release);
  // we need to use release semantic here to ensure that threads in try_get_value
  // that see the value written by this store also see the updated bucket_state.
  bucket->head.store(item, std::memory_order_relaxed);
  bucket->release_lock();
}

template <class Key, class Value, class... Policies>
vyukov_hash_map<Key, Value, Policies...>::iterator::iterator(iterator&& other) :
    block(std::move(other.block)),
    current_bucket(std::move(other.current_bucket)),
    current_bucket_state(std::move(other.current_bucket_state)),
    index(std::move(other.index)),
    extension(std::move(other.extension)),
    prev(std::move(other.prev)) {
  other.block.reset();
  other.current_bucket = nullptr;
  other.current_bucket_state = bucket_state();
  other.index = 0;
  other.extension = nullptr;
  other.prev = nullptr;
}

template <class Key, class Value, class... Policies>
auto vyukov_hash_map<Key, Value, Policies...>::iterator::operator=(iterator&& other) -> iterator& {
  block = std::move(other.block);
  current_bucket = std::move(other.current_bucket);
  current_bucket_state = std::move(other.current_bucket_state);
  index = std::move(other.index);
  extension = std::move(other.extension);
  prev = std::move(other.prev);

  other.block.reset();
  other.current_bucket = nullptr;
  other.current_bucket_state = bucket_state();
  other.index = 0;
  other.extension = nullptr;
  other.prev = nullptr;

  return *this;
}

template <class Key, class Value, class... Policies>
vyukov_hash_map<Key, Value, Policies...>::iterator::~iterator() {
  reset();
}

template <class Key, class Value, class... Policies>
void vyukov_hash_map<Key, Value, Policies...>::iterator::reset() {
  // unlock the current bucket
  if (current_bucket) {
    assert(current_bucket->state.load().is_locked());
    // (36) - this release-store synchronizes-with the acquire-CAS (7, 30, 34, 37) and the acquire-load (23)
    current_bucket->state.store(current_bucket_state, std::memory_order_release);
  }

  block.reset();
  current_bucket = nullptr;
  current_bucket_state = bucket_state();
  index = 0;
  extension = nullptr;
  prev = nullptr;
}

template <class Key, class Value, class... Policies>
bool vyukov_hash_map<Key, Value, Policies...>::iterator::operator==(const iterator& r) const {
  return block == r.block && current_bucket == r.current_bucket && current_bucket_state == r.current_bucket_state &&
         index == r.index && extension == r.extension;
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
    if (extension == nullptr) {
      move_to_next_bucket();
    }
    return *this;
  }

  ++index;
  if (index == current_bucket_state.item_count()) {
    prev = &current_bucket->head;
    extension = current_bucket->head.load(std::memory_order_relaxed);
    if (extension == nullptr) {
      move_to_next_bucket();
    }
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

  // TODO - no need to lock if bucket is empty

  backoff backoff;
  for (;;) {
    auto st = current_bucket->state.load(std::memory_order_relaxed);
    if (st.is_locked()) {
      continue;
    }

    // (37) - this acquire-CAS synchronizes-with the release-store (3, 5, 11, 13, 14, 36, 38)
    if (current_bucket->state.compare_exchange_strong(
          st, st.locked(), std::memory_order_acquire, std::memory_order_relaxed)) {
      current_bucket_state = st;
      break;
    }
    backoff();
  }

  // (38) - this release-store synchronizes-with the acquire-CAS (7, 30, 34, 37) and the acquire-load (23)
  old_bucket->state.store(old_bucket_state, std::memory_order_release); // unlock the previous bucket

  index = 0;
  extension = nullptr;
  prev = nullptr;
  if (current_bucket_state.item_count() == 0) {
    move_to_next_bucket();
  }
}

} // namespace xenium

#ifdef _MSC_VER
  #pragma warning(pop)
#endif
