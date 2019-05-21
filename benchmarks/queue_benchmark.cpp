
#include "benchmark.hpp"
#include "execution.hpp"
#include "config.hpp"
#include "descriptor.hpp"

#include <iostream>
#include <vector>

#ifdef WITH_RAMALHETE_QUEUE
#include <xenium/ramalhete_queue.hpp>
#endif

#ifdef WITH_MICHAEL_SCOTT_QUEUE
#include <xenium/michael_scott_queue.hpp>
#endif

using boost::property_tree::ptree;

#ifdef WITH_RAMALHETE_QUEUE
template <class T, class... Policies>
struct descriptor<xenium::ramalhete_queue<T, Policies...>> {
  static boost::property_tree::ptree generate() {
    using queue = xenium::ramalhete_queue<T, Policies...>;
    boost::property_tree::ptree pt;
    pt.put("type", "ramalhete_queue");
    pt.put_child("reclaimer", descriptor<typename queue::reclaimer>::generate());
    return pt;
  }
};
#endif

#ifdef WITH_MICHAEL_SCOTT_QUEUE
template <class T, class... Policies>
struct descriptor<xenium::michael_scott_queue<T, Policies...>> {
  static boost::property_tree::ptree generate() {
    using queue = xenium::michael_scott_queue<T, Policies...>;
    boost::property_tree::ptree pt;
    pt.put("type", "michael_scott_queue");
    pt.put_child("reclaimer", descriptor<typename queue::reclaimer>::generate());
    return pt;
  }
};
#endif

template <class T>
struct queue_benchmark;

template <class T>
struct benchmark_thread : execution_thread {
  benchmark_thread(queue_benchmark<T>& benchmark, std::uint32_t id, const execution& exec) :
    execution_thread(id, exec),
    _benchmark(benchmark)
  {}
  using Reclaimer = typename T::reclaimer;
  virtual void run() override;
  virtual std::string report() const {
    return "push: " + std::to_string(push_operations) + "; pop: " + std::to_string(pop_operations);
  }
protected:
  void set_pop_ratio(double ratio) {
    assert(ratio >= 0 && ratio <= 1);
    _pop_ratio = static_cast<unsigned>(ratio * (1 << ratio_bits));
  }
  unsigned push_operations = 0;
  unsigned pop_operations = 0;
private:
  queue_benchmark<T>& _benchmark;
  static constexpr unsigned ratio_bits = 8;
  unsigned _pop_ratio; // multiple of 2^ratio_bits;
};

template <class T>
struct push_thread : benchmark_thread<T> {
  push_thread(queue_benchmark<T>& benchmark, std::uint32_t id, const execution& exec) :
    benchmark_thread<T>(benchmark, id, exec)
  {}
  virtual void setup(const boost::property_tree::ptree& config) override {
    auto ratio = config.get<double>("pop_ratio", 0.0);
    if (ratio > 1.0 || ratio < 0.0)
      throw std::runtime_error("Invalid pop_ratio value");
    this->set_pop_ratio(ratio);
  }
};

template <class T>
struct pop_thread : benchmark_thread<T> {
  pop_thread(queue_benchmark<T>& benchmark, std::uint32_t id, const execution& exec) :
    benchmark_thread<T>(benchmark, id, exec)
  {}
  virtual void setup(const boost::property_tree::ptree& config) override {
    auto ratio = config.get<double>("push_ratio", 0.0);
    if (ratio > 1.0 || ratio < 0.0)
      throw std::runtime_error("Invalid push_ratio value");
    this->set_pop_ratio(1.0 - ratio);
  }
};

template <class T>
struct queue_benchmark : benchmark {
  virtual void setup(const boost::property_tree::ptree& config) override {
    auto prefill = config.get<std::uint32_t>("prefill", 10);
    (void)prefill;
    // TODO
  }

  virtual std::unique_ptr<execution_thread> create_thread(
    std::uint32_t id,
    const execution& exec,
    const std::string& type) override
  {
    if (type == "push")
      return std::make_unique<push_thread<T>>(*this, id, exec);
    else if (type == "pop")
      return std::make_unique<pop_thread<T>>(*this, id, exec);
    else
      throw std::runtime_error("Invalid thread type: " + type);
  }

  T queue;
  std::uint32_t number_of_elements = 100;
};

namespace {
#ifdef WITH_RAMALHETE_QUEUE
  template <class T, class... Policies>
  bool try_push(xenium::ramalhete_queue<T*, Policies...>& queue, T item) {
    queue.push(new T(item));
    return true;
  }

  template <class T, class... Policies>
  bool try_pop(xenium::ramalhete_queue<T*, Policies...>& queue, T& item) {
    T* value;
    auto result = queue.try_pop(value);
    if (result) {
      item = *value;
      delete value;
    }
    return result;
  }
#endif

#ifdef WITH_MICHAEL_SCOTT_QUEUE
  template <class T, class... Policies>
  bool try_push(xenium::michael_scott_queue<T, Policies...>& queue, T item) {
    queue.push(std::move(item));
    return true;
  }

  template <class T, class... Policies>
  bool try_pop(xenium::michael_scott_queue<T, Policies...>& queue, T& item) {
    return queue.try_pop(item);
  }
#endif
}

template <class T>
void benchmark_thread<T>::run()
{
  T& queue = _benchmark.queue;
  
  const std::uint32_t n = 100;
  const std::uint32_t number_of_keys = std::max(1u, _benchmark.number_of_elements * 2);

  unsigned push = 0;
  unsigned pop = 0;

  typename T::reclaimer::region_guard{};
  for (std::uint32_t i = 0; i < n; ++i) {
    auto r = _randomizer();
    auto action = r & ((1 << ratio_bits) - 1);
    std::uint32_t key = (r >> ratio_bits) % number_of_keys;

    if (action <= _pop_ratio) {
      unsigned value;
      if (try_pop(queue, value)) {
        ++pop;
      }
    } else if (try_push(queue, key)) {
        ++push;
    }
  }

  push_operations += push;
  pop_operations += pop;
}

namespace {
  template <class T>
  inline std::shared_ptr<benchmark_builder> make_benchmark_builder() {
    return std::make_shared<typed_benchmark_builder<T, queue_benchmark>>();
  }

  auto benchmark_variations()
  {
    using namespace xenium;
    return benchmark_builders
    {
#ifdef WITH_RAMALHETE_QUEUE
  #ifdef WITH_EPOCH_BASED
      make_benchmark_builder<ramalhete_queue<QUEUE_ITEM*, policy::reclaimer<reclamation::epoch_based<100>>>>(),
  #endif
#endif

#ifdef WITH_MICHAEL_SCOTT_QUEUE
  #ifdef WITH_EPOCH_BASED
      make_benchmark_builder<michael_scott_queue<QUEUE_ITEM, policy::reclaimer<reclamation::epoch_based<100>>>>(),
  #endif
#endif
    };
  }

  struct register_benchmark{
    register_benchmark() {
      benchmarks.emplace("queue", benchmark_variations());
    }
  } register_benchmark;
}