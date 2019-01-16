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
    bool TrivialKey,
    bool TrivialValue>
  struct vyukov_hash_map_traits;

  template <class Key, class Value, class ValueReclaimer, class Reclaimer>
  struct vyukov_hash_map_traits<Key, Value*, ValueReclaimer, Reclaimer, true, true> {
    static_assert(
      std::is_base_of<typename ValueReclaimer::template enable_concurrent_ptr<Value>, Value>::value,
      "if policy::value_reclaimer is specified, then Value must be a pointer to a type that inherits from value_reclaimer::enable_concurrent_ptr");

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

    static void store_value(value_type& cell, Value* v, std::memory_order order) {
      cell.store(v, order);
    }

    static void reclaim(accessor& a) { a.guard.reclaim(); }
    static void reclaim_internal(accessor& a) {} // noop
  };

  template <class Key, class Value, class Reclaimer>
  struct vyukov_hash_map_traits<Key, Value, parameter::nil, Reclaimer, true, true> {
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

    static void store_value(value_type& cell, Value v, std::memory_order order) {
      cell.store(v, order);
    }

    static void reclaim(accessor& a) {} // noop
    static void reclaim_internal(accessor& a) {} // noop
  };

  template <class Key, class Value, class ValueReclaimer, class Reclaimer>
  struct vyukov_hash_map_traits<Key, Value, ValueReclaimer, Reclaimer, true, false> {
    using reclaimer = std::conditional_t<
      std::is_same<ValueReclaimer, parameter::nil>::value, Reclaimer, ValueReclaimer>;

    struct node : reclaimer::template enable_concurrent_ptr<node> {
      node(Value&& value): value(std::move(value)) {}
      Value value;
    };

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

    static void store_value(value_type& cell, Value&& v, std::memory_order order) {
      cell.store(new node(std::move(v)), order);
    }

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
