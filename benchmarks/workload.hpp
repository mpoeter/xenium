#pragma once

#include <functional>
#include <memory>
#include <string>
#include <tao/config/value.hpp>
#include <unordered_map>

struct workload_simulator {
  virtual ~workload_simulator() = default;
  virtual void simulate() = 0;
};

class workload_factory {
public:
  workload_factory();
  std::shared_ptr<workload_simulator> operator()(const tao::config::value& config);
  using builder = std::function<std::shared_ptr<workload_simulator>(const tao::config::value&)>;
  void register_workload(std::string type, builder func);

private:
  std::unordered_map<std::string, builder> _builders;
};