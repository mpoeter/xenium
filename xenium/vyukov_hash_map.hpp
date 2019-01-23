//
// Copyright (c) 2018 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_VYUKOV_HASH_MAP_HPP
#define XENIUM_VYUKOV_HASH_MAP_HPP

#include <xenium/acquire_guard.hpp>
#include <xenium/backoff.hpp>
#include <xenium/parameter.hpp>
#include <xenium/policy.hpp>
#include <xenium/utils.hpp>

#include <atomic>
#include <cstdint>

#define XENIUM_VYUKOV_HASH_MAP_TRAITS
#include <xenium/impl/vyukov_hash_map_traits.hpp>
#undef XENIUM_VYUKOV_HASH_MAP_TRAITS

namespace xenium {

namespace policy {
  /**
   * @brief Policy to configure the reclaimer used for values stored in `vyukov_hash_map`.
   * @tparam T
   */
  template <class T>
  struct value_reclaimer;
}

namespace detail {
  template <class T>
  struct vyukov_supported_type {
    static constexpr bool value =
      std::is_trivial<T>::value && (sizeof(T) == 4 || sizeof(T) == 8);
  };
}

/**
 * @brief A concurrent hash-map that uses fine-grained locking.
 *
 * **This is a preliminary version; the interface will be subject to change.**
 *
 * This hash-map is heavily inspired by the hash-map presented by Vyukov
 * It uses bucket-level locking for update operations (`emplace`/`erase`); however, read-only
 * operations (`try_get_value`) are lock-free. Buckets are cacheline aligned to reduce false
 * sharing and minimize cache trashing.
 *
 * The current version only supports trivial types of size 4 or 8 as `Key` and `Value`.
 * Also, life-time management of keys/values is left entirely to the user. These limitations
 * will be lifted in future versions.
 *
 * Supported policies:
 *  * `xenium::policy::reclaimer`<br>
 *    Defines the reclamation scheme to be used for internal allocations. (**required**)
 *  * `xenium::policy::hash`<br>
 *    Defines the hash function. (*optional*; defaults to `std::hash<Key>`)
 *  * `xenium::policy::backoff`<br>
 *    Defines the backoff strategy. (*optional*; defaults to `xenium::no_backoff`)
 *
 * @tparam Key
 * @tparam Value
 * @tparam Policies
 */
template <class Key, class Value, class... Policies>
struct vyukov_hash_map {
  using reclaimer = parameter::type_param_t<policy::reclaimer, parameter::nil, Policies...>;
  using value_reclaimer = parameter::type_param_t<policy::value_reclaimer, parameter::nil, Policies...>;
  using hash = parameter::type_param_t<policy::hash, std::hash<Key>, Policies...>;
  using backoff = parameter::type_param_t<policy::backoff, no_backoff, Policies...>;

  template <class... NewPolicies>
  using with = vyukov_hash_map<Key, Value, NewPolicies..., Policies...>;

  static_assert(parameter::is_set<reclaimer>::value, "reclaimer policy must be specified");
  //static_assert(detail::vyukov_supported_type<Key>::value,
  //  "This version of vykov_hash_map only supports trivial types of size 4 or 8 as Key.");

private:
  using traits = typename impl::vyukov_hash_map_traits<Key, Value, value_reclaimer, reclaimer, hash,
    detail::vyukov_supported_type<Key>::value, detail::vyukov_supported_type<Value>::value>;

public:
  vyukov_hash_map(std::size_t initial_capacity = 128);
  ~vyukov_hash_map();

  class iterator;
  
  using accessor = typename traits::accessor;

  bool emplace(Key key, Value value);

  //template <class... Args>
  //std::pair<iterator, bool> get_or_emplace(Key key, Args&&... args);

  //template <class Factory>
  //std::pair<iterator, bool> get_or_emplace_lazy(Key key, Factory factory);

  bool extract(const Key& key, accessor& value);

  bool erase(const Key& key);
  void erase(iterator& pos);

  //iterator find(const Key& key);

  bool try_get_value(const Key& key, accessor& result) const;
  
  bool contains(const Key& key);

  //accessor operator[](const Key& key);

  iterator begin() {
    iterator result;
    result.current_bucket = &lock_bucket(0, result.block, result.current_bucket_state);
    if (result.current_bucket_state.item_count() == 0)
      result.move_to_next_bucket();
    return result;
  }
  iterator end() { return iterator(); }
private:
  using hash_t = std::size_t;

  struct bucket_state;
  struct bucket;
  struct extension_item;
  struct extension_bucket;
  struct block;
  using block_ptr = typename reclaimer::template concurrent_ptr<block, 0>;  
  using guarded_block = typename block_ptr::guard_ptr;

  
  static constexpr std::uint32_t bucket_to_extension_ratio = 128;
  static constexpr std::uint32_t bucket_item_count = 3;
  static constexpr std::uint32_t extension_item_count = 10;
  
  static constexpr std::size_t item_counter_bits = utils::find_last_bit_set(bucket_item_count);
  static constexpr std::size_t lock_bit = 2 * item_counter_bits + 1;
  static constexpr std::size_t version_shift = lock_bit;

  static constexpr std::uint32_t lock = 1u << (lock_bit - 1);
  static constexpr std::size_t version_inc = 1ul << lock_bit;
  
  static constexpr std::uint32_t item_count_mask = (1u << item_counter_bits) - 1;
  static constexpr std::uint32_t delete_item_mask = item_count_mask << item_counter_bits;

  block_ptr data_block;
  std::atomic<int> resize_lock;

  block* allocate_block(std::uint32_t bucket_count);
  
  bucket& lock_bucket(hash_t hash, guarded_block& block, bucket_state& state);
  void grow(bucket& bucket, bucket_state state);

  bool do_extract(const Key& key, accessor& value);
  
  static extension_item* allocate_extension_item(block* b, hash_t hash);
  static void free_extension_item(extension_item* item);
};

template <class Key, class Value, class... Policies>
class vyukov_hash_map<Key, Value, Policies...>::iterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = std::pair<const Key, Value>;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type;
  using reference = value_type;

  iterator();
  ~iterator();

  iterator(iterator&&) = default;
  iterator& operator=(iterator&&) = default;
  
  iterator(const iterator&) = delete;
  iterator& operator=(const iterator&) = delete;

  bool operator==(const iterator& r) const;
  bool operator!=(const iterator& r) const;
  iterator& operator++();

  reference operator*();
  pointer operator->();

  void reset();
private:
  guarded_block block;
  bucket* current_bucket;
  bucket_state current_bucket_state;
  std::uint32_t index;
  extension_item* extension;
  std::atomic<extension_item*>* prev;
  friend class vyukov_hash_map;

  void move_to_next_bucket();
  Value* erase_current();
};

}

#define XENIUM_VYUKOV_HASH_MAP_IMPL
#include <xenium/impl/vyukov_hash_map.hpp>
#undef XENIUM_VYUKOV_HASH_MAP_IMPL

#endif
