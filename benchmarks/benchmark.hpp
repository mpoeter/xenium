#pragma once

#include "descriptor.hpp"

#include <tao/config/value.hpp>

#include <functional>
#include <unordered_map>
#include <vector>

struct execution;
struct execution_thread;

namespace config {
struct prefill {
  bool serial = false;
  std::uint64_t count = 0;
  void setup(const tao::config::value& config, std::uint64_t default_count);
  [[nodiscard]] std::uint64_t get_thread_quota(std::uint32_t thread_id, std::uint32_t num_threads) const;
};
} // namespace config

struct benchmark {
  virtual ~benchmark() = default;
  virtual void setup(const tao::config::value& config) = 0;
  virtual std::unique_ptr<execution_thread>
    create_thread(std::uint32_t id, const execution& exec, const std::string& type) = 0;
};

struct benchmark_builder {
  virtual ~benchmark_builder() = default;
  // returns a json object describing the configuration of this benchmark
  virtual tao::json::value get_descriptor() = 0;
  virtual std::shared_ptr<benchmark> build() = 0;
};

template <class T, template <class> class Benchmark>
struct typed_benchmark_builder : benchmark_builder {
  tao::json::value get_descriptor() override { return descriptor<T>::generate(); }
  std::shared_ptr<benchmark> build() override { return std::make_shared<Benchmark<T>>(); }
};

template <class T>
struct region_guard {
  using type = typename T::reclaimer::region_guard;
};

template <class T>
using region_guard_t = typename region_guard<T>::type;

using benchmark_builders = std::vector<std::shared_ptr<benchmark_builder>>;
using registered_benchmarks = std::unordered_map<std::string, benchmark_builders>;
