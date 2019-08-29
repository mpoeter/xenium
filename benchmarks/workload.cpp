#include "workload.hpp"

#include <boost/property_tree/ptree.hpp>

#include <cstdint>

using boost::property_tree::ptree;

namespace {

struct dummy_workload_simulator : workload_simulator{
  dummy_workload_simulator(std::uint32_t iterations) :
    _iterations(iterations)
  {}
  dummy_workload_simulator(const ptree& config) :
    _iterations(config.get<std::uint32_t>("iterations"))
  {}

  void simulate() override {
    // use volatile here to prevent the compiler from eliminating the empty loop
    for (volatile std::uint32_t i = 0; i < _iterations; ++i) {
      if (i % 64) {
        // TODO - pause?
      }
    }
  }
private:
  std::uint32_t _iterations;
};

template <class T>
workload_factory::builder make_builder() {
  return [](const ptree& config) -> std::shared_ptr<workload_simulator> {
    return std::make_shared<T>(config);
  };
}

}

workload_factory::workload_factory() {
  register_workload("dummy", make_builder<dummy_workload_simulator>());
}

void workload_factory::register_workload(std::string type, builder func) {
  _builders.emplace(std::move(type), std::move(func));
}

std::shared_ptr<workload_simulator> workload_factory::operator()(
  const boost::property_tree::ptree& config)
{
  if (config.empty()) {
    auto iterations = config.get_value<std::uint32_t>();
    return std::make_shared<dummy_workload_simulator>(iterations);
  }

  auto type = config.get<std::string>("type");
  auto it = _builders.find(type);
  if (it == _builders.end())
    throw std::runtime_error("Invalid workload type " + type);
  return it->second(config);
}