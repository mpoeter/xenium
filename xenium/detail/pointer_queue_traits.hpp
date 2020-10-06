//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_POINTER_QUEUE_TRAITS_HPP
#define XENIUM_POINTER_QUEUE_TRAITS_HPP

#include <cstring>
#include <memory>
#include <type_traits>

namespace xenium::detail {

template <class T, class... Policies>
struct trivially_copyable_pointer_queue_traits {
  static_assert(std::is_trivially_copyable<T>::value && sizeof(T) < sizeof(void*));
  using value_type = T;
  using raw_type = void**;
  static raw_type get_raw(value_type& val) {
    raw_type result = nullptr;
    // TODO - handle endianess correctly
    // need to cast to void* to avoid gcc error about "copying an object of non-trivial type"
    std::memcpy(&result, static_cast<void*>(&val), sizeof(value_type));
    return result;
  }
  static void release(value_type&) {}
  static void store(value_type& target, raw_type val) {
    // TODO - handle endianess correctly
    // need to cast to void* to avoid gcc error about "copying an object of non-trivial type"
    std::memcpy(static_cast<void*>(&target), &val, sizeof(value_type));
  }
  static void delete_value(raw_type) {}
};

// TODO - specialization for trivially copyable types smaller than void*
template <class T, class... Policies>
struct pointer_queue_traits {
  static_assert(std::is_pointer<T>::value, "T must be a raw pointer type or a std::unique_ptr");
};

template <class T, class... Policies>
struct pointer_queue_traits<T*, Policies...> {
  using value_type = T*;
  using raw_type = T*;
  static raw_type get_raw(T* val) { return val; }
  static void release(value_type) {}
  static void store(value_type& target, raw_type val) { target = val; }
  static void delete_value(raw_type) {}
};

template <class T, class... Policies>
struct pointer_queue_traits<std::unique_ptr<T>, Policies...> {
  using value_type = std::unique_ptr<T>;
  using raw_type = T*;
  static raw_type get_raw(value_type& val) { return val.get(); }
  static void release(value_type& val) { (void)val.release(); }
  static void store(value_type& target, raw_type val) { target.reset(val); }
  static void delete_value(raw_type v) { std::unique_ptr<T> dummy{v}; }
};

template <class T, class... Policies>
using pointer_queue_traits_t = std::conditional_t<std::is_trivially_copyable<T>::value && sizeof(T) < sizeof(void*),
                                                  trivially_copyable_pointer_queue_traits<T, Policies...>,
                                                  pointer_queue_traits<T, Policies...>>;
} // namespace xenium::detail
#endif
