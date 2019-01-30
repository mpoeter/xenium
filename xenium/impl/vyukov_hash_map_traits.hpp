//
// Copyright (c) 2018 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_VYUKOV_HASH_MAP_IMPL
#error "This is an impl file and must not be included directly!"
#endif

namespace xenium { namespace impl {
  template <class Key, class Value, class VReclaimer, class ValueReclaimer, class Reclaimer, class Hash>
  struct vyukov_hash_map_traits<Key, managed_ptr<Value, VReclaimer>, ValueReclaimer, Reclaimer, Hash, true, true> {
    static_assert(!parameter::is_set<ValueReclaimer>::value,
      "value_reclaimer policy can only be used with non-trivial key/value types");
    
    using key_type = Key;
    using value_type = Value*;
    using storage_key_type = std::atomic<Key>;
    using storage_value_type = typename VReclaimer::template concurrent_ptr<Value>;
    using iterator_value_type = std::pair<const Key, Value>;
    using iterator_reference = iterator_value_type;

    class accessor {
    public:
      accessor() = default;
      Value* operator->() const noexcept { return guard.get(); }
      Value& operator*() const noexcept { return guard.get(); }
      void reset() { guard.reset(); }
      void reclaim() { guard.reclaim(); }
    private:
      accessor(storage_value_type& v, std::memory_order order):
        guard(acquire_guard(v, order))
      {}
      typename storage_value_type::guard_ptr guard;
      friend struct vyukov_hash_map_traits;
    };

    static accessor acquire(storage_value_type& v, std::memory_order order) {
      return accessor(v, order);
    }

    static void store_item(storage_key_type& key_cell, storage_value_type& value_cell,
      std::size_t hash, Key k, Value* v, std::memory_order order)
    {
      key_cell.store(k, std::memory_order_relaxed);
      value_cell.store(v, order);
    }

    static bool compare_key_and_get_accessor(storage_key_type& key_cell, storage_value_type& value_cell, const Key& key,
      std::size_t hash, accessor& acc)
    {
      if (key_cell.load(std::memory_order_relaxed) != key)
        return false;
      acc.guard = typename storage_value_type::guard_ptr(value_cell.load(std::memory_order_relaxed));
      return true;
    }

    static bool compare_trivial_key(storage_key_type& key_cell, const Key& key, std::size_t hash) {
      return key_cell.load(std::memory_order_relaxed) == key;
    }

    static bool compare_nontrivial_key(const accessor& acc, const Key& key) { return true; }

    static iterator_reference deref_iterator(storage_key_type& k, storage_value_type& v) {
      return {k.load(std::memory_order_relaxed), v.load(std::memory_order_relaxed)};
    }

    static std::size_t rehash(Key k) { return Hash{}(k); }

    static void reclaim(accessor& a) { a.guard.reclaim(); }
    static void reclaim_internal(accessor& a) {} // noop
  };

  template <class Key, class Value, class VReclaimer, class Reclaimer, class Hash>
  struct vyukov_hash_map_traits<Key, Value, VReclaimer, Reclaimer, Hash, true, true> {
    static_assert(!parameter::is_set<ValueReclaimer>::value,
      "value_reclaimer policy can only be used with non-trivial key/value types");

    using key_type = Key;
    using value_type = Value;
    using storage_key_type = std::atomic<Key>;
    using storage_value_type = std::atomic<Value>;
    using iterator_value_type = std::pair<const Key, Value>;
    using iterator_reference = iterator_value_type;

    using accessor = Value;

    static accessor acquire(storage_value_type& v, std::memory_order order) { return v.load(order); }

    static void store_item(storage_key_type& key_cell, storage_value_type& value_cell,
      std::size_t hash, Key k, Value v, std::memory_order order)
    {
      key_cell.store(k, std::memory_order_relaxed);
      value_cell.store(v, order);
    }

    static bool compare_key_and_get_accessor(storage_key_type& key_cell, storage_value_type& value_cell,
      const Key& key, std::size_t hash, accessor& acc)
    {
      if (key_cell.load(std::memory_order_relaxed) != key)
        return false;
      acc = value_cell.load(std::memory_order_relaxed);
      return true;
    }

    static bool compare_trivial_key(storage_key_type& key_cell, const Key& key, std::size_t hash) {
      return key_cell.load(std::memory_order_relaxed) == key;
    }

    static bool compare_nontrivial_key(const accessor& acc, const Key& key) { return true; }

    static iterator_reference deref_iterator(storage_key_type& k, storage_value_type& v) {
      return {k.load(std::memory_order_relaxed), v.load(std::memory_order_relaxed)};
    }

    static std::size_t rehash(Key k) { return Hash{}(k); }

    static void reclaim(accessor& a) {} // noop
    static void reclaim_internal(accessor& a) {} // noop
  };

  template <class Key, class Value, class ValueReclaimer, class Reclaimer, class Hash>
  struct vyukov_hash_map_traits<Key, Value, ValueReclaimer, Reclaimer, Hash, true, false> {
    using reclaimer = std::conditional_t<
      std::is_same<ValueReclaimer, parameter::nil>::value, Reclaimer, ValueReclaimer>;

    struct node : reclaimer::template enable_concurrent_ptr<node> {
      node(Value&& value): value(std::move(value)) {}
      Value value;
    };

    using key_type = Key;
    using value_type = Value;
    using storage_key_type = std::atomic<Key>;
    using storage_value_type = typename reclaimer::template concurrent_ptr<node>;
    using iterator_value_type = std::pair<const Key, Value&>;
    using iterator_reference = iterator_value_type;

    class accessor {
    public:
      accessor() = default;
      Value* operator->() const noexcept { return &guard->value; }
      Value& operator*() const noexcept { return guard->value; }
      void reset() { guard.reset(); }
    private:
      accessor(storage_value_type& v, std::memory_order order):
        guard(acquire_guard(v, order))
      {}
      typename storage_value_type::guard_ptr guard;
      friend struct vyukov_hash_map_traits;
    };

    static accessor acquire(storage_value_type& v, std::memory_order order) {
      return accessor(v, order);
    }

    static bool compare_key_and_get_accessor(storage_key_type& key_cell, storage_value_type& value_cell,
      const Key& key, std::size_t hash, accessor& acc)
    {
      if (key_cell.load(std::memory_order_relaxed) != key)
        return false;
      acc.guard = typename storage_value_type::guard_ptr(value_cell.load(std::memory_order_relaxed));
      return true;
    }

    static bool compare_trivial_key(storage_key_type& key_cell, const Key& key, std::size_t hash) {
      return key_cell.load(std::memory_order_relaxed) == key;
    }

    static bool compare_nontrivial_key(const accessor& acc, const Key& key) { return true; }

    static void store_item(storage_key_type& key_cell, storage_value_type& value_cell,
      std::size_t hash, Key&& k, Value&& v, std::memory_order order)
    {
      key_cell.store(k, std::memory_order_relaxed);
      value_cell.store(new node(std::move(v)), order);
    }

    static iterator_reference deref_iterator(storage_key_type& k, storage_value_type& v) {
      auto node = v.load(std::memory_order_relaxed);
      return {k.load(std::memory_order_relaxed), node->value};
    }

    static std::size_t rehash(Key k) { return Hash{}(k); }

    static void reclaim(accessor& a) { a.guard.reclaim(); }
    static void reclaim_internal(accessor& a) {
      // copy guard to avoid resetting the accessor's guard_ptr.
      // TODO - this could be simplified by avoiding reset of
      // guard_ptrs in reclaim().
      auto g = a.guard;
      g.reclaim();
    }
  };

  template <class Key, class Value, class ValueReclaimer, class Reclaimer, class Hash, bool TrivialValue>
  struct vyukov_hash_map_traits<Key, Value, ValueReclaimer, Reclaimer, Hash, false, TrivialValue> {
    using reclaimer = std::conditional_t<
      std::is_same<ValueReclaimer, parameter::nil>::value, Reclaimer, ValueReclaimer>;

    struct node : reclaimer::template enable_concurrent_ptr<node> {
      node(Key&& key, Value&& value):
        data(std::move(key), std::move(value))
      {}

      std::pair<const Key, Value> data;
    };

    using key_type = Key;
    using value_type = Value;
    using storage_key_type = std::atomic<std::size_t>;
    using storage_value_type = typename reclaimer::template concurrent_ptr<node>;
    using iterator_value_type = std::pair<const Key, Value>;
    using iterator_reference = iterator_value_type&;

    class accessor {
    public:
      accessor() = default;
      Value* operator->() const noexcept { return &guard->data.second; }
      Value& operator*() const noexcept { return guard->data.second; }
      void reset() { guard.reset(); }
    private:
      accessor(storage_value_type& v, std::memory_order order):
        guard(acquire_guard(v, order))
      {}
      typename storage_value_type::guard_ptr guard;
      friend struct vyukov_hash_map_traits;
    };

    static accessor acquire(storage_value_type& v, std::memory_order order) {
      return accessor(v, order);
    }

    static void store_item(storage_key_type& key_cell, storage_value_type& value_cell,
      std::size_t hash, Key&& k, Value&& v, std::memory_order order)
    {
      key_cell.store(hash, std::memory_order_relaxed);
      value_cell.store(new node(std::move(k), std::move(v)), order);
    }

    static bool compare_key_and_get_accessor(storage_key_type& key_cell, storage_value_type& value_cell,
      const Key& key, std::size_t hash, accessor& acc)
    {
      if (key_cell.load(std::memory_order_relaxed) != hash)
        return false;
      acc.guard = typename storage_value_type::guard_ptr(value_cell.load(std::memory_order_relaxed));
      return acc.guard->data.first == key;
    }

    static bool compare_trivial_key(storage_key_type& key_cell, const Key& key, std::size_t hash) {
      return key_cell.load(std::memory_order_relaxed) == hash;
    }

    static bool compare_nontrivial_key(const accessor& acc, const Key& key) {
       return acc.guard->data.first == key;
    }

    static iterator_reference deref_iterator(storage_key_type& k, storage_value_type& v) {
      auto node = v.load(std::memory_order_relaxed);
      return node->data;
    }

    static std::size_t rehash(std::size_t h) { return h; }

    static void reclaim(accessor& a) { a.guard.reclaim(); }
    static void reclaim_internal(accessor& a) {
      // copy guard to avoid resetting the accessor's guard_ptr.
      // TODO - this could be simplified by avoiding reset of
      // guard_ptrs in reclaim().
      auto g = a.guard;
      g.reclaim();
    }
  };
}}
