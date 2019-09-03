//
// Copyright (c) 2018 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_RAMALHETE_QUEUE_IMPL
#error "This is an impl file and must not be included directly!"
#endif

#include <memory>

namespace xenium { namespace impl {

template <class T, class... Policies>
struct ramalhete_queue_traits {
  static_assert(std::is_pointer<T>::value, "T must be a raw pointer type or a std::unique_ptr");
};

template <class T, class... Policies>
struct ramalhete_queue_traits<T*, Policies...> {
  using raw_type = T*;
  static raw_type get_raw(T* val) { return val; }
  static void release(T*) {}
  static void store(T*& target, T* val) { target = val; }
  static void delete_value(raw_type) {}
}; 

template <class T, class... Policies>
struct ramalhete_queue_traits<std::unique_ptr<T>, Policies...> {
  using value_type = std::unique_ptr<T>;
  using raw_type = T*;
  static raw_type get_raw(value_type& val) { return val.get(); }
  static void release(value_type& val) { val.release(); }
  static void store(value_type& target, T* val) { target.reset(val); }
  static void delete_value(raw_type v) { value_type{v}; }
};

}}