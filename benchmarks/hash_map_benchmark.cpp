#include "benchmark.hpp"
#include "execution.hpp"
#include "config.hpp"
#include "descriptor.hpp"

#include <iostream>
#include <vector>

#ifdef WITH_VYUKOV_HASH_MAP
#include <xenium/vyukov_hash_map.hpp>
#endif

#ifdef WITH_HARRIS_MICHAEL_HASH_MAP
#include <xenium/harris_michael_hash_map.hpp>
#endif

#ifdef WITH_LIBCDS
#include <cds/gc/hp.h>
#include <cds/gc/dhp.h>
#endif

#ifdef WITH_CDS_MICHAEL_MAP
#include <cds/container/michael_kvlist_hp.h>
#include <cds/container/michael_map.h>
#endif

#ifdef WITH_CDS_FELDMAN_HASHMAP
#include <cds/container/feldman_hashmap_hp.h>
#include <cds/container/feldman_hashmap_dhp.h>
#include <cds/container/feldman_hashmap_rcu.h>
#endif

using boost::property_tree::ptree;

template <class T>
struct hash_map_builder {
  static auto create(const boost::property_tree::ptree&) { return std::make_unique<T>(); }
};

template <class T>
struct region_guard {
  using type = typename T::reclaimer::region_guard;
};

template <class T>
using region_guard_t = typename region_guard<T>::type;

#ifdef WITH_VYUKOV_HASH_MAP
template <class Key, class Value, class... Policies>
struct descriptor<xenium::vyukov_hash_map<Key, Value, Policies...>> {
  static boost::property_tree::ptree generate() {
    using hash_map = xenium::vyukov_hash_map<Key, Value, Policies...>;
    boost::property_tree::ptree pt;
    pt.put("type", "vyukov_hash_map");
    pt.put_child("reclaimer", descriptor<typename hash_map::reclaimer>::generate());
    return pt;
  }
};

namespace {
  template <class Key, class Value, class... Policies>
  bool try_emplace(xenium::vyukov_hash_map<Key, Value, Policies...>& hash_map, std::uint64_t key) {
    return hash_map.emplace(key, key);
  }

  template <class Key, class Value, class... Policies>
  bool try_remove(xenium::vyukov_hash_map<Key, Value, Policies...>& hash_map, std::uint64_t key) {
    return hash_map.erase(key);
  }

  template <class Key, class Value, class... Policies>
  bool try_get(xenium::vyukov_hash_map<Key, Value, Policies...>& hash_map, std::uint64_t key) {
    typename xenium::vyukov_hash_map<Key, Value, Policies...>::accessor acc;
    return hash_map.try_get_value(key, acc);
  }
}
#endif

#ifdef WITH_HARRIS_MICHAEL_HASH_MAP
template <class Key, class Value, class... Policies>
struct descriptor<xenium::harris_michael_hash_map<Key, Value, Policies...>> {
  static boost::property_tree::ptree generate() {
    using hash_map = xenium::harris_michael_hash_map<Key, Value, Policies...>;
    boost::property_tree::ptree pt;
    pt.put("type", "harris_michael_hash_map");
    pt.put_child("reclaimer", descriptor<typename hash_map::reclaimer>::generate());
    return pt;
  }
};

namespace {
  template <class Key, class Value, class... Policies>
  bool try_emplace(xenium::harris_michael_hash_map<Key, Value, Policies...>& hash_map, std::uint64_t key) {
    return hash_map.emplace(key, key);
  }

  template <class Key, class Value, class... Policies>
  bool try_remove(xenium::harris_michael_hash_map<Key, Value, Policies...>& hash_map, std::uint64_t key) {
    return hash_map.erase(key);
  }

  template <class Key, class Value, class... Policies>
  bool try_get(xenium::harris_michael_hash_map<Key, Value, Policies...>& hash_map, std::uint64_t key) {
    auto it = hash_map.find(key);
    return it != hash_map.end();
  }
}
#endif


#ifdef WITH_CDS_MICHAEL_MAP
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

template <class T>
struct hash_map_benchmark;

template <class T>
struct benchmark_thread : execution_thread {
  benchmark_thread(hash_map_benchmark<T>& benchmark, std::uint32_t id, const execution& exec) :
    execution_thread(id, exec),
    _benchmark(benchmark)
  {}
  virtual void setup(const boost::property_tree::ptree& config) override {
    execution_thread::setup(config);

    _key_range = config.get<std::uint64_t>("key_range", _benchmark.key_range);
    _key_offset = config.get<std::uint64_t>("key_offset", _benchmark.key_offset);
    
    auto remove_ratio = config.get<double>("remove_ratio", 0.2);
    if (remove_ratio < 0.0 || remove_ratio > 1.0)
      throw std::runtime_error("remove_ratio must be >= 0.0 and <= 1.0");

    auto insert_ratio = config.get<double>("insert_ratio", 0.2);
    if (insert_ratio < 0.0 || insert_ratio > 1.0)
      throw std::runtime_error("insert_ratio must be >= 0.0 and <= 1.0");

    auto update_ratio = remove_ratio + insert_ratio;
    if (update_ratio > 1.0)
      throw std::runtime_error("The sum of remove_ratio and insert_ratio must be <= 1.0");

    auto rand_range = std::numeric_limits<std::uint64_t>::max();
    _scale_insert = static_cast<std::uint64_t>(insert_ratio * rand_range);
    _scale_remove = static_cast<std::uint64_t>(update_ratio * rand_range);
  }
  virtual void initialize(std::uint32_t num_threads) override;
  virtual void run() override;
  virtual thread_report report() const {
    boost::property_tree::ptree data;
    data.put("runtime", _runtime.count());
    data.put("insert", insert_operations);
    data.put("remove", remove_operations);
    data.put("get", get_operations);
    return { data, insert_operations + remove_operations + get_operations };
  }
protected:
  std::uint32_t insert_operations = 0;
  std::uint32_t remove_operations = 0;
  std::uint32_t get_operations = 0;
private:
  hash_map_benchmark<T>& _benchmark;
  
  std::uint64_t _key_range;
  std::uint64_t _key_offset;
  std::uint64_t _scale_remove;
  std::uint64_t _scale_insert;
};

template <class T>
struct hash_map_benchmark : benchmark {
  virtual void setup(const boost::property_tree::ptree& config) override;

  virtual std::unique_ptr<execution_thread> create_thread(
    std::uint32_t id,
    const execution& exec,
    const std::string& type) override
  {
    if (type == "mixed")
      return std::make_unique<benchmark_thread<T>>(*this, id, exec);
    
    throw std::runtime_error("Invalid thread type: " + type);
  }

  std::unique_ptr<T> hash_map;
  std::uint64_t key_range;
  std::uint64_t key_offset;
  config::prefill prefill;
};

template <class T>
void hash_map_benchmark<T>::setup(const boost::property_tree::ptree& config) {
  hash_map = hash_map_builder<T>::create(config.get_child("ds"));
  key_range = config.get<std::uint64_t>("key_range", 2048);
  key_offset = config.get<std::uint64_t>("key_offset", 0);
  
  // by default we prefill 10% of the configured key-range
  prefill.setup(config, key_range / 10);
  if (this->prefill.count > key_range)
    throw std::runtime_error("prefill.count must be less or equal key_range");
}

template <class T>
void benchmark_thread<T>::initialize(std::uint32_t num_threads) {
  auto id = this->id() & execution::thread_id_mask;
  std::uint32_t cnt = _benchmark.prefill.get_thread_quota(id, num_threads);

  region_guard_t<T>{};
  auto step_size = _benchmark.key_range / _benchmark.prefill.count;
  std::uint64_t key = id * step_size + _benchmark.key_offset;
  step_size *= num_threads;
  for (std::uint64_t i = 0 ; i < cnt; ++i, key += step_size) {
    if (!try_emplace(*_benchmark.hash_map, key)) {
      throw initialization_failure();
    }
  }
}

template <class T>
void benchmark_thread<T>::run() {
  T& hash_map = *_benchmark.hash_map;
  
  // TODO - make n configurable
  const std::uint32_t n = 100;

  std::uint32_t insert = 0;
  std::uint32_t remove = 0;
  std::uint32_t get = 0;

  region_guard_t<T>{};
  for (std::uint32_t i = 0; i < n; ++i) {
    auto r = _randomizer();
    auto key = (r % _key_range) + _key_offset;

    if (r < _scale_insert) {
      if (try_emplace(hash_map, key))
        ++insert;
    } else if (r < _scale_remove) {
      if (try_remove(hash_map, key))
        ++remove;
    } else {
      if (try_get(hash_map, key))
        ++get;
    }

    simulate_workload();
  }

  insert_operations += insert;
  remove_operations += remove;
  get_operations += get;
}

namespace {
  template <class T>
  inline std::shared_ptr<benchmark_builder> make_benchmark_builder() {
    return std::make_shared<typed_benchmark_builder<T, hash_map_benchmark>>();
  }

  auto benchmark_variations()
  {
    using namespace xenium;
    return benchmark_builders
    {
#ifdef WITH_VYUKOV_HASH_MAP
  #ifdef WITH_EPOCH_BASED
      make_benchmark_builder<vyukov_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::epoch_based<100>>>>(),
  #endif
  #ifdef WITH_NEW_EPOCH_BASED
      make_benchmark_builder<vyukov_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::new_epoch_based<100>>>>(),
  #endif
  #ifdef WITH_QUIESCENT_STATE_BASED
    make_benchmark_builder<vyukov_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::quiescent_state_based>>>(),
  #endif
  #ifdef WITH_DEBRA
    make_benchmark_builder<vyukov_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::debra<100>>>>(),
  #endif
  #ifdef WITH_HAZARD_POINTER
    make_benchmark_builder<
      vyukov_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<
        reclamation::hazard_pointer<reclamation::static_hazard_pointer_policy<3>>>>>(),
    make_benchmark_builder<
      vyukov_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<
        reclamation::hazard_pointer<reclamation::dynamic_hazard_pointer_policy<3>>>>>(),
  #endif
#endif

#ifdef WITH_HARRIS_MICHAEL_HASH_MAP
  #ifdef WITH_EPOCH_BASED
      make_benchmark_builder<harris_michael_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::epoch_based<100>>>>(),
  #endif
  #ifdef WITH_NEW_EPOCH_BASED
      make_benchmark_builder<harris_michael_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::new_epoch_based<100>>>>(),
  #endif
  #ifdef WITH_QUIESCENT_STATE_BASED
    make_benchmark_builder<harris_michael_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::quiescent_state_based>>>(),
  #endif
  #ifdef WITH_DEBRA
    make_benchmark_builder<harris_michael_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::debra<100>>>>(),
  #endif

  #ifdef WITH_HAZARD_POINTER
    make_benchmark_builder<
      harris_michael_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<
        reclamation::hazard_pointer<reclamation::static_hazard_pointer_policy<3>>>>>(),
    make_benchmark_builder<
      harris_michael_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<
        reclamation::hazard_pointer<reclamation::dynamic_hazard_pointer_policy<3>>>>>(),
  #endif
#endif

#ifdef WITH_CDS_MICHAEL_MAP
    make_benchmark_builder<
      cds::container::MichaelHashMap<cds::gc::HP,
        cds::container::MichaelKVList< cds::gc::HP, int, int>>>(),
#endif

#ifdef WITH_CDS_FELDMAN_HASHMAP
    make_benchmark_builder<cds::container::FeldmanHashMap<cds::gc::HP, QUEUE_ITEM, QUEUE_ITEM>>(),
#endif
    };
  }
}

void register_hash_map_benchmark(registered_benchmarks& benchmarks) {
  benchmarks.emplace("hash_map", benchmark_variations());
}