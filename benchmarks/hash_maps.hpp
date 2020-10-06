#include "benchmark.hpp"
#include "descriptor.hpp"
#include "reclaimers.hpp"

template <class T>
struct hash_map_builder {
  static auto create(const tao::config::value&) { return std::make_unique<T>(); }
};

#ifdef WITH_VYUKOV_HASH_MAP
  #include <xenium/vyukov_hash_map.hpp>

template <class Key, class Value, class... Policies>
struct descriptor<xenium::vyukov_hash_map<Key, Value, Policies...>> {
  static tao::json::value generate() {
    using hash_map = xenium::vyukov_hash_map<Key, Value, Policies...>;
    return {{"type", "vyukov_hash_map"},
            {"initial_capacity", DYNAMIC_PARAM},
            {"reclaimer", descriptor<typename hash_map::reclaimer>::generate()}};
  }
};

template <class Key, class Value, class... Policies>
struct hash_map_builder<xenium::vyukov_hash_map<Key, Value, Policies...>> {
  static auto create(const tao::config::value& config) {
    auto initial_capacity = config.optional<size_t>("initial_capacity").value_or(128);
    return std::make_unique<xenium::vyukov_hash_map<Key, Value, Policies...>>(initial_capacity);
  }
};

namespace { // NOLINT
template <class Key, class Value, class... Policies>
bool try_emplace(xenium::vyukov_hash_map<Key, Value, Policies...>& hash_map, Key key) {
  return hash_map.emplace(key, key);
}

template <class Key, class Value, class... Policies>
bool try_remove(xenium::vyukov_hash_map<Key, Value, Policies...>& hash_map, Key key) {
  return hash_map.erase(key);
}

template <class Key, class Value, class... Policies>
bool try_get(xenium::vyukov_hash_map<Key, Value, Policies...>& hash_map, Key key) {
  typename xenium::vyukov_hash_map<Key, Value, Policies...>::accessor acc;
  return hash_map.try_get_value(key, acc);
}
} // namespace
#endif

#ifdef WITH_HARRIS_MICHAEL_HASH_MAP
  #include <xenium/harris_michael_hash_map.hpp>

template <class Key, class Value, class... Policies>
struct descriptor<xenium::harris_michael_hash_map<Key, Value, Policies...>> {
  static tao::json::value generate() {
    using hash_map = xenium::harris_michael_hash_map<Key, Value, Policies...>;
    return {{"type", "harris_michael_hash_map"},
            {"buckets", hash_map::num_buckets},
            {"reclaimer", descriptor<typename hash_map::reclaimer>::generate()}};
  }
};

namespace { // NOLINT
template <class Key, class Value, class... Policies>
bool try_emplace(xenium::harris_michael_hash_map<Key, Value, Policies...>& hash_map, Key key) {
  return hash_map.emplace(key, key);
}

template <class Key, class Value, class... Policies>
bool try_remove(xenium::harris_michael_hash_map<Key, Value, Policies...>& hash_map, Key key) {
  return hash_map.erase(key);
}

template <class Key, class Value, class... Policies>
bool try_get(xenium::harris_michael_hash_map<Key, Value, Policies...>& hash_map, Key key) {
  auto it = hash_map.find(key);
  return it != hash_map.end();
}
} // namespace
#endif

#ifdef WITH_LIBCDS
  #include <cds/gc/dhp.h>
  #include <cds/gc/hp.h>
#endif

#ifdef WITH_CDS_MICHAEL_HASHMAP
  #include <cds/container/michael_kvlist_hp.h>
  #include <cds/container/michael_map.h>

template <class List, class Traits>
struct descriptor<cds::container::MichaelHashMap<cds::gc::HP, List, Traits>> {
  static tao::json::value generate() {
    return {
      {"type", "cds::MichaelHashMap"}, {"nMaxItemCount", DYNAMIC_PARAM}, {"nLoadFactor", DYNAMIC_PARAM}, {"gc", "HP"}};
  }
};

template <class GC, class List, class Traits>
struct hash_map_builder<cds::container::MichaelHashMap<GC, List, Traits>> {
  static auto create(const tao::config::value& config) {
    auto nMaxItemCount = config.as<size_t>("nMaxItemCount");
    auto nLoadFactor = config.as<size_t>("nLoadFactor");
    return std::make_unique<cds::container::MichaelHashMap<GC, List, Traits>>(nMaxItemCount, nLoadFactor);
  }
};

template <class GC, class List, class Traits>
struct region_guard<cds::container::MichaelHashMap<GC, List, Traits>> {
  struct type {};
};

namespace {
template <class GC, class List, class Traits>
bool try_emplace(cds::container::MichaelHashMap<GC, List, Traits>& hash_map, std::uint64_t key) {
  return hash_map.emplace(key, key);
}

template <class GC, class List, class Traits>
bool try_remove(cds::container::MichaelHashMap<GC, List, Traits>& hash_map, std::uint64_t key) {
  return hash_map.erase(key);
}

template <class GC, class List, class Traits>
bool try_get(cds::container::MichaelHashMap<GC, List, Traits>& hash_map, std::uint64_t key) {
  return hash_map.find(key, [](auto& /*val*/) {});
}
} // namespace
#endif

#ifdef WITH_CDS_FELDMAN_HASHMAP
  #include <cds/container/feldman_hashmap_dhp.h>
  #include <cds/container/feldman_hashmap_hp.h>
  #include <cds/container/feldman_hashmap_rcu.h>

template <class Key, class T, class Traits>
struct descriptor<cds::container::FeldmanHashMap<cds::gc::HP, Key, T, Traits>> {
  static tao::json::value generate() { return {{"type", "cds::FeldmanHashMap"}, {"gc", "HP"}}; }
};

template <class GC, class Key, class T, class Traits>
struct region_guard<cds::container::FeldmanHashMap<GC, Key, T, Traits>> {
  struct type {};
};

namespace {
template <class GC, class Key, class T, class Traits>
bool try_emplace(cds::container::FeldmanHashMap<GC, Key, T, Traits>& hash_map, std::uint64_t key) {
  return hash_map.emplace(key, key);
}

template <class GC, class Key, class T, class Traits>
bool try_remove(cds::container::FeldmanHashMap<GC, Key, T, Traits>& hash_map, std::uint64_t key) {
  return hash_map.erase(key);
}

template <class GC, class Key, class T, class Traits>
bool try_get(cds::container::FeldmanHashMap<GC, Key, T, Traits>& hash_map, std::uint64_t key) {
  return hash_map.find(key, [](auto& /*val*/) {});
}
} // namespace
#endif
