#include "benchmark.hpp"
#include "descriptor.hpp"
#include "reclaimers.hpp"

template <class T>
struct hash_map_builder {
  static auto create(const boost::property_tree::ptree&) { return std::make_unique<T>(); }
};

#ifdef WITH_VYUKOV_HASH_MAP
#include <xenium/vyukov_hash_map.hpp>

template <class Key, class Value, class... Policies>
struct hash_map_builder<xenium::vyukov_hash_map<Key, Value, Policies...>> {
  static auto create(const boost::property_tree::ptree& config) {
    auto initial_capacity = config.get<size_t>("initial_capacity", 128);
    return std::make_unique<xenium::vyukov_hash_map<Key, Value, Policies...>>(initial_capacity);
  }
};

namespace {
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
}
#endif

#ifdef WITH_HARRIS_MICHAEL_HASH_MAP
#include <xenium/harris_michael_hash_map.hpp>

template <class Key, class Value, class... Policies>
struct descriptor<xenium::harris_michael_hash_map<Key, Value, Policies...>> {
  static boost::property_tree::ptree generate() {
    using hash_map = xenium::harris_michael_hash_map<Key, Value, Policies...>;
    boost::property_tree::ptree pt;
    pt.put("type", "harris_michael_hash_map");
    auto buckets = hash_map::num_buckets;
    pt.put("buckets", buckets);
    pt.put_child("reclaimer", descriptor<typename hash_map::reclaimer>::generate());
    return pt;
  }
};

namespace {
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
}
#endif

#ifdef WITH_LIBCDS
#include <cds/gc/hp.h>
#include <cds/gc/dhp.h>
#endif

#ifdef WITH_CDS_MICHAEL_MAP
#include <cds/container/michael_kvlist_hp.h>
#include <cds/container/michael_map.h>

template <class GC, class List, class Traits>
struct descriptor<cds::container::MichaelHashMap<GC, List, Traits>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "cds::MichaelMap");
    pt.put("nMaxItemCount", DYNAMIC_PARAM);
    pt.put("nLoadFactor", DYNAMIC_PARAM);  
    return pt;
  }
};

template <class GC, class List, class Traits>
struct hash_map_builder<cds::container::MichaelHashMap<GC, List, Traits>> {
  static auto create(const boost::property_tree::ptree& config) {
    auto nMaxItemCount = config.get<size_t>("nMaxItemCount");
    auto nLoadFactor = config.get<size_t>("nLoadFactor");
    return std::make_unique<cds::container::MichaelHashMap<GC, List, Traits>>(nMaxItemCount, nLoadFactor);
  }
};

template <class GC, class List, class Traits>
struct region_guard<cds::container::MichaelHashMap<GC, List, Traits>> {
  struct type{};
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
    return hash_map.find(key, [](auto& /*val*/){});
  }
}
#endif

#ifdef WITH_CDS_FELDMAN_HASHMAP
#include <cds/container/feldman_hashmap_hp.h>
#include <cds/container/feldman_hashmap_dhp.h>
#include <cds/container/feldman_hashmap_rcu.h>

template <class GC, class Key, class T, class Traits>
struct descriptor<cds::container::FeldmanHashMap<GC, Key, T, Traits>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "cds::FeldmanHashMap");
    return pt;
  }
};

template <class GC, class Key, class T, class Traits>
struct region_guard<cds::container::FeldmanHashMap<GC, Key, T, Traits>> {
  struct type{};
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
    return hash_map.find(key, [](auto& /*val*/){});
  }
}
#endif
