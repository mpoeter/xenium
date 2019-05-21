#pragma once

#include "descriptor.hpp"

#include <boost/property_tree/ptree_fwd.hpp>

#include <unordered_map>
#include <vector>
#include <functional>

struct execution;
struct execution_thread;

struct benchmark
{
  virtual ~benchmark() {}
  virtual void setup(const boost::property_tree::ptree& config) = 0;
  virtual std::unique_ptr<execution_thread> create_thread(
    std::uint32_t id,
    const execution& exec,
    const std::string& type) = 0;
};

struct benchmark_builder {
  // returns a ptree describing the configuration of this benchmark
  virtual boost::property_tree::ptree get_descriptor() = 0;
  virtual std::shared_ptr<benchmark> build() = 0;
};

template <class T, template <class> class Benchmark>
struct typed_benchmark_builder : benchmark_builder {
  boost::property_tree::ptree get_descriptor() override { return descriptor<T>::generate(); }
  std::shared_ptr<benchmark> build() override { return std::make_shared<Benchmark<T>>(); }
};

using benchmark_builders = std::vector<std::shared_ptr<benchmark_builder>>;

extern std::unordered_map<std::string, benchmark_builders> benchmarks;
