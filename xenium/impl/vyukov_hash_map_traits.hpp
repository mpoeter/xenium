//
// Copyright (c) 2018 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_VYUKOV_HASH_MAP_TRAITS
#error "This is an impl file and must not be included directly!"
#endif

namespace xenium { namespace impl {
  template <class Value, class ValueReclaimer>
  struct vyukov_value_traits;

  template <class Value, class ValueReclaimer>
  struct vyukov_value_traits<Value*, ValueReclaimer> {
    static_assert(
      std::is_base_of<typename ValueReclaimer::template enable_concurrent_ptr<Value>, Value>::value,
      "if policy::value_reclaimer is specified, then Value must be a pointer to a type that inherits from value_reclaimer::enable_concurrent_ptr");

    using type = typename ValueReclaimer::template concurrent_ptr<Value>;

    class accessor {
    public:
      accessor() = default;
      Value* operator->() const noexcept { return guard.get(); }
      Value& operator*() const noexcept { return guard.get(); }
      void reset() { guard.reset(); }
      void reclaim() { guard.reclaim(); }
    private:
      accessor(type& v, std::memory_order order):
        guard(acquire_guard(v, order))
      {}
      typename type::guard_ptr guard;
      template <class, class>
      friend struct vyukov_value_traits;
    };

    static accessor acquire(type& v, std::memory_order order) {
      return accessor(v, order);
    }

    static void reclaim(accessor& a) {
      a.guard.reclaim();
    }
  };

  template <class Value>
  struct vyukov_value_traits<Value, parameter::nil>  {
    using type = std::atomic<Value>;

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

    static accessor acquire(type& v, std::memory_order order) {
      return v.load(order);
    }

    static void reclaim(accessor& a) {} // noop
  };
}}
