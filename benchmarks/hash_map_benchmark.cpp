#include "benchmark.hpp"
#include "config.hpp"
#include "execution.hpp"
#include "hash_maps.hpp"

#include <iostream>
#include <vector>

using config_t = tao::config::value;

template <class T>
struct hash_map_benchmark;

template <class T>
struct benchmark_thread : execution_thread {
  benchmark_thread(hash_map_benchmark<T>& benchmark, std::uint32_t id, const execution& exec) :
      execution_thread(id, exec),
      _benchmark(benchmark) {}
  void setup(const config_t& config) override {
    execution_thread::setup(config);

    _key_range = config.optional<std::uint64_t>("key_range").value_or(_benchmark.key_range);
    _key_offset = config.optional<std::uint64_t>("key_offset").value_or(_benchmark.key_offset);

    auto remove_ratio = config.optional<double>("remove_ratio").value_or(0.2);
    if (remove_ratio < 0.0 || remove_ratio > 1.0) {
      throw std::runtime_error("remove_ratio must be >= 0.0 and <= 1.0");
    }

    auto insert_ratio = config.optional<double>("insert_ratio").value_or(0.2);
    if (insert_ratio < 0.0 || insert_ratio > 1.0) {
      throw std::runtime_error("insert_ratio must be >= 0.0 and <= 1.0");
    }

    auto update_ratio = remove_ratio + insert_ratio;
    if (update_ratio > 1.0) {
      throw std::runtime_error("The sum of remove_ratio and insert_ratio must be <= 1.0");
    }

    constexpr auto rand_range = std::numeric_limits<std::uint64_t>::max();
    _scale_insert = static_cast<std::uint64_t>(insert_ratio * static_cast<double>(rand_range));
    _scale_remove = static_cast<std::uint64_t>(update_ratio * static_cast<double>(rand_range));
  }
  void initialize(std::uint32_t num_threads) override;
  void run() override;
  [[nodiscard]] thread_report report() const override {
    tao::json::value data{
      {"runtime", _runtime.count()},
      {"insert", insert_operations},
      {"remove", remove_operations},
      {"get", get_operations},
    };
    return {data, insert_operations + remove_operations + get_operations};
  }

protected:
  std::uint64_t insert_operations = 0;
  std::uint64_t remove_operations = 0;
  std::uint64_t get_operations = 0;

private:
  hash_map_benchmark<T>& _benchmark;

  std::uint64_t _key_range = 0;
  std::uint64_t _key_offset = 0;
  std::uint64_t _scale_remove = 0;
  std::uint64_t _scale_insert = 0;
};

template <class T>
struct hash_map_benchmark : benchmark {
  void setup(const config_t& config) override;

  std::unique_ptr<execution_thread>
    create_thread(std::uint32_t id, const execution& exec, const std::string& type) override {
    if (type == "mixed") {
      return std::make_unique<benchmark_thread<T>>(*this, id, exec);
    }

    throw std::runtime_error("Invalid thread type: " + type);
  }

  std::unique_ptr<T> hash_map;
  std::uint32_t batch_size = 0;
  std::uint64_t key_range = 0;
  std::uint64_t key_offset = 0;
  config::prefill prefill{};
};

template <class T>
void hash_map_benchmark<T>::setup(const tao::config::value& config) {
  hash_map = hash_map_builder<T>::create(config.at("ds"));
  batch_size = config.optional<std::uint32_t>("batch_size").value_or(100);
  key_range = config.optional<std::uint64_t>("key_range").value_or(2048);
  key_offset = config.optional<std::uint64_t>("key_offset").value_or(0);

  // by default we prefill 10% of the configured key-range
  prefill.setup(config, key_range / 10);
  if (this->prefill.count > key_range) {
    throw std::runtime_error("prefill.count must be less or equal key_range");
  }
}

template <class T>
void benchmark_thread<T>::initialize(std::uint32_t num_threads) {
  auto id = this->id() & execution::thread_id_mask;
  std::uint64_t cnt = _benchmark.prefill.get_thread_quota(id, num_threads);

  [[maybe_unused]] region_guard_t<T> guard{};
  auto step_size = _benchmark.key_range / _benchmark.prefill.count;
  std::uint64_t key = id * step_size + _benchmark.key_offset;
  step_size *= num_threads;
  for (std::uint64_t i = 0; i < cnt; ++i, key += step_size) {
    if (!try_emplace(*_benchmark.hash_map, static_cast<unsigned>(key))) {
      throw initialization_failure();
    }
  }
}

template <class T>
void benchmark_thread<T>::run() {
  T& hash_map = *_benchmark.hash_map;

  const std::uint32_t n = _benchmark.batch_size;

  std::uint32_t insert = 0;
  std::uint32_t remove = 0;
  std::uint32_t get = 0;

  [[maybe_unused]] region_guard_t<T> guard{};
  for (std::uint32_t i = 0; i < n; ++i) {
    auto r = _randomizer();
    auto key = static_cast<unsigned>((r % _key_range) + _key_offset);

    if (r < _scale_insert) {
      if (try_emplace(hash_map, key)) {
        ++insert;
      }
    } else if (r < _scale_remove) {
      if (try_remove(hash_map, key)) {
        ++remove;
      }
    } else {
      if (try_get(hash_map, key)) {
        ++get;
      }
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

auto benchmark_variations() {
  using namespace xenium; // NOLINT
  return benchmark_builders{
#ifdef WITH_VYUKOV_HASH_MAP
  #ifdef WITH_GENERIC_EPOCH_BASED
    make_benchmark_builder<vyukov_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::epoch_based<>>>>(),
    make_benchmark_builder<
      vyukov_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::new_epoch_based<>>>>(),
    make_benchmark_builder<vyukov_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::debra<>>>>(),
  #endif
  #ifdef WITH_QUIESCENT_STATE_BASED
    make_benchmark_builder<
      vyukov_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::quiescent_state_based>>>(),
  #endif
  #ifdef WITH_HAZARD_POINTER
    make_benchmark_builder<
      vyukov_hash_map<QUEUE_ITEM,
                      QUEUE_ITEM,
                      policy::reclaimer<reclamation::hazard_pointer<>::with<
                        policy::allocation_strategy<reclamation::hp_allocation::static_strategy<3>>>>>>(),
    make_benchmark_builder<
      vyukov_hash_map<QUEUE_ITEM,
                      QUEUE_ITEM,
                      policy::reclaimer<reclamation::hazard_pointer<>::with<
                        policy::allocation_strategy<reclamation::hp_allocation::dynamic_strategy<3>>>>>>(),
  #endif
#endif

#ifdef WITH_HARRIS_MICHAEL_HASH_MAP
  #ifdef WITH_GENERIC_EPOCH_BASED
    make_benchmark_builder<
      harris_michael_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::epoch_based<>>>>(),
    make_benchmark_builder<
      harris_michael_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::new_epoch_based<>>>>(),
    make_benchmark_builder<harris_michael_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::debra<>>>>(),
  #endif
  #ifdef WITH_QUIESCENT_STATE_BASED
    make_benchmark_builder<
      harris_michael_hash_map<QUEUE_ITEM, QUEUE_ITEM, policy::reclaimer<reclamation::quiescent_state_based>>>(),
  #endif

  #ifdef WITH_HAZARD_POINTER
    make_benchmark_builder<
      harris_michael_hash_map<QUEUE_ITEM,
                              QUEUE_ITEM,
                              policy::reclaimer<reclamation::hazard_pointer<>::with<
                                policy::allocation_strategy<reclamation::hp_allocation::static_strategy<3>>>>>>(),
    make_benchmark_builder<
      harris_michael_hash_map<QUEUE_ITEM,
                              QUEUE_ITEM,
                              policy::reclaimer<reclamation::hazard_pointer<>::with<
                                policy::allocation_strategy<reclamation::hp_allocation::dynamic_strategy<3>>>>>>(),
  #endif
#endif

#ifdef WITH_CDS_MICHAEL_HASHMAP
    make_benchmark_builder<
      cds::container::MichaelHashMap<cds::gc::HP,
                                     cds::container::MichaelKVList<cds::gc::HP, QUEUE_ITEM, QUEUE_ITEM>>>(),
#endif

#ifdef WITH_CDS_FELDMAN_HASHMAP
    make_benchmark_builder<cds::container::FeldmanHashMap<cds::gc::HP, QUEUE_ITEM, QUEUE_ITEM>>(),
#endif
  };
}
} // namespace

void register_hash_map_benchmark(registered_benchmarks& benchmarks) {
  benchmarks.emplace("hash_map", benchmark_variations());
}