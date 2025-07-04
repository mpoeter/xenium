//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_META_HPP
#define XENIUM_META_HPP

#include <cstddef>
#include <utility>

namespace xenium::meta {

template <class... Ts>
struct type_list {
  static constexpr std::size_t size = sizeof...(Ts);
};

namespace detail {
  template <typename A, typename B>
  struct union_list;

  template <typename... As, typename... Bs>
  struct union_list<type_list<As...>, type_list<Bs...>> {
    using type = type_list<As..., Bs...>;
  };

  template <class T1, class... Ts>
  struct build_cross_product {
    using type = type_list<std::pair<T1, Ts>...>;
  };

  template <class A, class B>
  struct cross_product;

  template <class... Bs>
  struct cross_product<type_list<>, type_list<Bs...>> {
    using type = type_list<>;
  };

  template <class A1, class... As, class... Bs>
  struct cross_product<type_list<A1, As...>, type_list<Bs...>> {
    using type = typename union_list<typename build_cross_product<A1, Bs...>::type,
                                     typename cross_product<type_list<As...>, type_list<Bs...>>::type>::type;
  };

  template <class List, template <class> class Map>
  struct map;

  template <class... Ts, template <class> class Map>
  struct map<type_list<Ts...>, Map> {
    using type = type_list<typename Map<Ts>::type...>;
  };

} // namespace detail

template <class T1, class T2>
using cross_product = typename detail::cross_product<T1, T2>::type;

template <class List, template <class> class Map>
using map = typename detail::map<List, Map>::type;

} // namespace xenium::meta

#endif
