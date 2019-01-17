//
// Copyright (c) 2018 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_VYUKOV_HASH_MAP_TRAITS
#error "This is an impl file and must not be included directly!"
#endif

namespace xenium { namespace impl {
  template <
    class Key,
    class Value,
    class ValueReclaimer,
    class Reclaimer,
    class Hash,
    bool TrivialKey,
    bool TrivialValue>
  struct vyukov_hash_map_traits;

  template <class Key, class Value, class ValueReclaimer, class Reclaimer, class Hash>
  struct vyukov_hash_map_traits<Key, Value*, ValueReclaimer, Reclaimer, Hash, true, true> {
    static_assert(
      std::is_base_of<typename ValueReclaimer::template enable_concurrent_ptr<Value>, Value>::value,
      "if policy::value_reclaimer is specified, then Value must be a pointer to a type that inherits from value_reclaimer::enable_concurrent_ptr");

    using key_type = std::atomic<Key>;
    using value_type = typename ValueReclaimer::template concurrent_ptr<Value>;

    class accessor {
    public:
      accessor() = default;
      Value* operator->() const noexcept { return guard.get(); }
      Value& operator*() const noexcept { return guard.get(); }
      void reset() { guard.reset(); }
      void reclaim() { guard.reclaim(); }
    private:
      accessor(value_type& v, std::memory_order order):
        guard(acquire_guard(v, order))
      {}
      typename value_type::guard_ptr guard;
      friend struct vyukov_hash_map_traits;
    };

    static accessor acquire(value_type& v, std::memory_order order) {
      return accessor(v, order);
    }

    static void store_item(key_type& key_cell, value_type& value_cell,
      std::size_t hash, Key k, Value* v, std::memory_order order)
    {
      key_cell.store(k, std::memory_order_relaxed);
      value_cell.store(v, order);
    }

    static bool compare_key_and_get_accessor(key_type& key_cell, value_type& value_cell, const Key& key,
      std::size_t hash, accessor& acc)
    {
      if (key_cell.load(std::memory_order_relaxed) != key)
        return false;
      acc.guard = typename value_type::guard_ptr(value_cell.load(std::memory_order_relaxed));
      return true;
    }

    static bool compare_trivial_key(key_type& key_cell, const Key& key, std::size_t hash) {
      return key_cell.load(std::memory_order_relaxed) == key;
    }

    static bool compare_nontrivial_key(const accessor& acc, const Key& key) { return true; }

    static std::size_t rehash(Key k) { return Hash{}(k); }

    static void reclaim(accessor& a) { a.guard.reclaim(); }
    static void reclaim_internal(accessor& a) {} // noop
  };

  template <class Key, class Value, class Reclaimer, class Hash>
  struct vyukov_hash_map_traits<Key, Value, parameter::nil, Reclaimer, Hash, true, true> {
    using key_type = std::atomic<Key>;
    using value_type = std::atomic<Value>;

   /*class accessor {
    public:
      accessor() = default;
      Value operator->() const noexcept { return v; }
      Value operator*() const noexcept { return v; }
    private:
      accessor(type& v):
        v(v.load(std::memory_order_acquire))
      {}
      Value v;
    };*/

    using accessor = Value;

    static accessor acquire(value_type& v, std::memory_order order) {
      return v.load(order);
    }

    static void store_item(key_type& key_cell, value_type& value_cell,
      std::size_t hash, Key k, Value v, std::memory_order order)
    {
      key_cell.store(k, std::memory_order_relaxed);
      value_cell.store(v, order);
    }

    static bool compare_key_and_get_accessor(key_type& key_cell, value_type& value_cell, const Key& key,
      std::size_t hash, accessor& acc)
    {
      if (key_cell.load(std::memory_order_relaxed) != key)
        return false;
      acc = value_cell.load(std::memory_order_relaxed);
      return true;
    }

    static bool compare_trivial_key(key_type& key_cell, const Key& key, std::size_t hash) {
      return key_cell.load(std::memory_order_relaxed) == key;
    }

    static bool compare_nontrivial_key(const accessor& acc, const Key& key) { return true; }


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

    using key_type = std::atomic<Key>;
    using value_type = typename reclaimer::template concurrent_ptr<node>;

    class accessor {
    public:
      accessor() = default;
      Value* operator->() const noexcept { return &guard->value; }
      Value& operator*() const noexcept { return guard->value; }
      void reset() { guard.reset(); }
    private:
      accessor(value_type& v, std::memory_order order):
        guard(acquire_guard(v, order))
      {}
      typename value_type::guard_ptr guard;
      friend struct vyukov_hash_map_traits;
    };

    static accessor acquire(value_type& v, std::memory_order order) {
      return accessor(v, order);
    }

    static bool compare_key_and_get_accessor(key_type& key_cell, value_type& value_cell, const Key& key,
      std::size_t hash, accessor& acc)
    {
      if (key_cell.load(std::memory_order_relaxed) != key)
        return false;
      acc.guard = typename value_type::guard_ptr(value_cell.load(std::memory_order_relaxed));
      return true;
    }

    static bool compare_trivial_key(key_type& key_cell, const Key& key, std::size_t hash) {
      return key_cell.load(std::memory_order_relaxed) == key;
    }

    static bool compare_nontrivial_key(const accessor& acc, const Key& key) { return true; }

    static void store_item(key_type& key_cell, value_type& value_cell,
      std::size_t hash, Key&& k, Value&& v, std::memory_order order)
    {
      key_cell.store(k, std::memory_order_relaxed);
      value_cell.store(new node(std::move(v)), order);
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
        key(std::move(key)),
        value(std::move(value))
      {}

      Key key;
      Value value;
    };

    using key_type = std::atomic<std::size_t>;
    using value_type = typename reclaimer::template concurrent_ptr<node>;

    class accessor {
    public:
      accessor() = default;
      Value* operator->() const noexcept { return &guard->value; }
      Value& operator*() const noexcept { return guard->value; }
      void reset() { guard.reset(); }
    private:
      accessor(value_type& v, std::memory_order order):
        guard(acquire_guard(v, order))
      {}
      typename value_type::guard_ptr guard;
      friend struct vyukov_hash_map_traits;
    };

    static accessor acquire(value_type& v, std::memory_order order) {
      return accessor(v, order);
    }

    static void store_item(key_type& key_cell, value_type& value_cell,
      std::size_t hash, Key&& k, Value&& v, std::memory_order order)
    {
      key_cell.store(hash, std::memory_order_relaxed);
      value_cell.store(new node(std::move(k), std::move(v)), order);
    }

    static bool compare_key_and_get_accessor(key_type& key_cell, value_type& value_cell, const Key& key,
      std::size_t hash, accessor& acc)
    {
      if (key_cell.load(std::memory_order_relaxed) != hash)
        return false;
      acc.guard = typename value_type::guard_ptr(value_cell.load(std::memory_order_relaxed));
      return acc.guard->key == key;
    }

    static bool compare_trivial_key(key_type& key_cell, const Key& key, std::size_t hash) {
      return key_cell.load(std::memory_order_relaxed) == hash;
    }

    static bool compare_nontrivial_key(const accessor& acc, const Key& key) {
       return acc.guard->key == key;
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
