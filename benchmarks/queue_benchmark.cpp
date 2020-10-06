
#include "benchmark.hpp"
#include "config.hpp"
#include "execution.hpp"
#include "queues.hpp"

#include <iostream>
#include <vector>

using config_t = tao::config::value;

template <class T>
struct queue_benchmark;

template <class T>
struct benchmark_thread : execution_thread {
  benchmark_thread(queue_benchmark<T>& benchmark, std::uint32_t id, const execution& exec) :
      execution_thread(id, exec),
      _benchmark(benchmark) {}
  void initialize(std::uint32_t num_threads) override;
  void run() override;
  [[nodiscard]] thread_report report() const override {
    tao::json::value data{
      {"runtime", _runtime.count()},
      {"push", push_operations},
      {"pop", pop_operations},
    };
    return {data, push_operations + pop_operations};
  }

protected:
  void set_pop_ratio(double ratio) {
    assert(ratio >= 0 && ratio <= 1);
    _pop_ratio = static_cast<unsigned>(ratio * (static_cast<unsigned>(1) << ratio_bits));
  }
  std::size_t push_operations = 0;
  std::size_t pop_operations = 0;

private:
  queue_benchmark<T>& _benchmark;
  static constexpr unsigned ratio_bits = 8;
  unsigned _pop_ratio; // multiple of 2^ratio_bits;
};

template <class T>
struct push_thread : benchmark_thread<T> {
  push_thread(queue_benchmark<T>& benchmark, std::uint32_t id, const execution& exec) :
      benchmark_thread<T>(benchmark, id, exec) {}
  void setup(const config_t& config) override {
    benchmark_thread<T>::setup(config);
    auto ratio = config.optional<double>("pop_ratio").value_or(0.0);
    if (ratio > 1.0 || ratio < 0.0) {
      throw std::runtime_error("Invalid pop_ratio value");
    }
    this->set_pop_ratio(ratio);
  }
};

template <class T>
struct pop_thread : benchmark_thread<T> {
  pop_thread(queue_benchmark<T>& benchmark, std::uint32_t id, const execution& exec) :
      benchmark_thread<T>(benchmark, id, exec) {}
  void setup(const config_t& config) override {
    benchmark_thread<T>::setup(config);
    auto ratio = config.optional<double>("push_ratio").value_or(0.0);
    if (ratio > 1.0 || ratio < 0.0) {
      throw std::runtime_error("Invalid push_ratio value");
    }
    this->set_pop_ratio(1.0 - ratio);
  }
};

template <class T>
struct queue_benchmark : benchmark {
  void setup(const config_t& config) override;

  std::unique_ptr<execution_thread>
    create_thread(std::uint32_t id, const execution& exec, const std::string& type) override {
    if (type == "producer") {
      return std::make_unique<push_thread<T>>(*this, id, exec);
    }
    if (type == "consumer") {
      return std::make_unique<pop_thread<T>>(*this, id, exec);
    }
    throw std::runtime_error("Invalid thread type: " + type);
  }

  std::unique_ptr<T> queue;
  std::uint32_t number_of_elements = 100;
  std::uint32_t batch_size;
  config::prefill prefill;
};

template <class T>
void queue_benchmark<T>::setup(const config_t& config) {
  queue = queue_builder<T>::create(config.at("ds"));
  batch_size = config.optional<std::uint32_t>("batch_size").value_or(100);
  prefill.setup(config, 100);
}

template <class T>
void benchmark_thread<T>::initialize(std::uint32_t num_threads) {
  auto id = this->id() & execution::thread_id_mask;
  std::uint64_t cnt = _benchmark.prefill.get_thread_quota(id, num_threads);

  [[maybe_unused]] region_guard_t<T> guard{};
  for (std::uint64_t i = 0, j = 0; i < cnt; ++i, j += 2) {
    if (!try_push(*_benchmark.queue, static_cast<unsigned>(j))) {
      throw initialization_failure();
    }
  }
}

template <class T>
void benchmark_thread<T>::run() {
  T& queue = *_benchmark.queue;

  const std::uint32_t n = _benchmark.batch_size;
  const std::uint32_t number_of_keys = std::max(1u, _benchmark.number_of_elements * 2);

  unsigned push = 0;
  unsigned pop = 0;

  [[maybe_unused]] region_guard_t<T> guard{};
  for (std::uint32_t i = 0; i < n; ++i) {
    auto r = _randomizer();
    auto action = r & ((1 << ratio_bits) - 1);
    std::uint32_t key = (r >> ratio_bits) % number_of_keys;

    if (action < _pop_ratio) {
      unsigned value;
      if (try_pop(queue, value)) {
        ++pop;
      }
    } else if (try_push(queue, key)) {
      ++push;
    }
    simulate_workload();
  }

  push_operations += push;
  pop_operations += pop;
}

namespace {
template <class T>
inline std::shared_ptr<benchmark_builder> make_benchmark_builder() {
  return std::make_shared<typed_benchmark_builder<T, queue_benchmark>>();
}

auto benchmark_variations() {
  using namespace xenium; // NOLINT
  return benchmark_builders{
#ifdef WITH_RAMALHETE_QUEUE
  #ifdef WITH_GENERIC_EPOCH_BASED
    make_benchmark_builder<ramalhete_queue<QUEUE_ITEM*, policy::reclaimer<reclamation::epoch_based<>>>>(),
    make_benchmark_builder<ramalhete_queue<QUEUE_ITEM*, policy::reclaimer<reclamation::new_epoch_based<>>>>(),
    make_benchmark_builder<ramalhete_queue<QUEUE_ITEM*, policy::reclaimer<reclamation::debra<>>>>(),
  #endif
  #ifdef WITH_QUIESCENT_STATE_BASED
    make_benchmark_builder<ramalhete_queue<QUEUE_ITEM*, policy::reclaimer<reclamation::quiescent_state_based>>>(),
  #endif
  #ifdef WITH_HAZARD_POINTER
    make_benchmark_builder<
      ramalhete_queue<QUEUE_ITEM*,
                      policy::reclaimer<reclamation::hazard_pointer<>::with<
                        policy::allocation_strategy<reclamation::hp_allocation::static_strategy<3>>>>>>(),
    make_benchmark_builder<
      ramalhete_queue<QUEUE_ITEM*,
                      policy::reclaimer<reclamation::hazard_pointer<>::with<
                        policy::allocation_strategy<reclamation::hp_allocation::dynamic_strategy<3>>>>>>(),
  #endif
#endif

#ifdef WITH_MICHAEL_SCOTT_QUEUE
  #ifdef WITH_GENERIC_EPOCH_BASED
    make_benchmark_builder<michael_scott_queue<QUEUE_ITEM, policy::reclaimer<reclamation::epoch_based<>>>>(),
    make_benchmark_builder<michael_scott_queue<QUEUE_ITEM, policy::reclaimer<reclamation::new_epoch_based<>>>>(),
    make_benchmark_builder<michael_scott_queue<QUEUE_ITEM, policy::reclaimer<reclamation::debra<>>>>(),
  #endif
  #ifdef WITH_QUIESCENT_STATE_BASED
    make_benchmark_builder<michael_scott_queue<QUEUE_ITEM, policy::reclaimer<reclamation::quiescent_state_based>>>(),
  #endif
  #ifdef WITH_HAZARD_POINTER
    make_benchmark_builder<
      michael_scott_queue<QUEUE_ITEM,
                          policy::reclaimer<reclamation::hazard_pointer<>::with<
                            policy::allocation_strategy<reclamation::hp_allocation::static_strategy<3>>>>>>(),
    make_benchmark_builder<
      michael_scott_queue<QUEUE_ITEM,
                          policy::reclaimer<reclamation::hazard_pointer<>::with<
                            policy::allocation_strategy<reclamation::hp_allocation::dynamic_strategy<3>>>>>>(),
  #endif
#endif

#ifdef WITH_VYUKOV_BOUNDED_QUEUE
    make_benchmark_builder<vyukov_bounded_queue<QUEUE_ITEM, policy::default_to_weak<true>>>(),
    make_benchmark_builder<vyukov_bounded_queue<QUEUE_ITEM, policy::default_to_weak<false>>>(),
#endif

#ifdef WITH_KIRSCH_KFIFO_QUEUE
  #ifdef WITH_GENERIC_EPOCH_BASED
    make_benchmark_builder<kirsch_kfifo_queue<QUEUE_ITEM*, policy::reclaimer<reclamation::epoch_based<>>>>(),
    make_benchmark_builder<kirsch_kfifo_queue<QUEUE_ITEM*, policy::reclaimer<reclamation::new_epoch_based<>>>>(),
    make_benchmark_builder<kirsch_kfifo_queue<QUEUE_ITEM*, policy::reclaimer<reclamation::debra<>>>>(),
  #endif
  #ifdef WITH_QUIESCENT_STATE_BASED
    make_benchmark_builder<kirsch_kfifo_queue<QUEUE_ITEM*, policy::reclaimer<reclamation::quiescent_state_based>>>(),
  #endif
  #ifdef WITH_HAZARD_POINTER
    make_benchmark_builder<
      kirsch_kfifo_queue<QUEUE_ITEM*,
                         policy::reclaimer<reclamation::hazard_pointer<>::with<
                           policy::allocation_strategy<reclamation::hp_allocation::static_strategy<3>>>>>>(),
    make_benchmark_builder<
      kirsch_kfifo_queue<QUEUE_ITEM*,
                         policy::reclaimer<reclamation::hazard_pointer<>::with<
                           policy::allocation_strategy<reclamation::hp_allocation::dynamic_strategy<3>>>>>>(),
  #endif
#endif

#ifdef WITH_KIRSCH_BOUNDED_KFIFO_QUEUE
    make_benchmark_builder<kirsch_bounded_kfifo_queue<QUEUE_ITEM*>>(),
#endif

#ifdef WITH_NIKOLAEV_BOUNDED_QUEUE
    make_benchmark_builder<nikolaev_bounded_queue<QUEUE_ITEM>>(),
#endif

#ifdef WITH_CDS_MSQUEUE
    make_benchmark_builder<cds::container::MSQueue<cds::gc::HP, QUEUE_ITEM>>(),
#endif

#ifdef WITH_CDS_BASKET_QUEUE
    make_benchmark_builder<cds::container::BasketQueue<cds::gc::HP, QUEUE_ITEM>>(),
#endif

#ifdef WITH_CDS_SEGMENTED_QUEUE
    make_benchmark_builder<cds::container::SegmentedQueue<cds::gc::HP, QUEUE_ITEM>>(),
#endif

#ifdef WITH_BOOST_LOCKFREE_QUEUE
    make_benchmark_builder<boost::lockfree::queue<QUEUE_ITEM>>(),
#endif
  };
}
} // namespace

void register_queue_benchmark(registered_benchmarks& benchmarks) {
  benchmarks.emplace("queue", benchmark_variations());
}
