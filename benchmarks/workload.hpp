#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <boost/property_tree/ptree_fwd.hpp>

struct workload_simulator {
  virtual ~workload_simulator() = default;
  virtual void simulate() = 0;
};

class workload_factory {
public:
  workload_factory();
  std::shared_ptr<workload_simulator> operator()(const boost::property_tree::ptree& config);
  using builder = std::function<
    std::shared_ptr<workload_simulator>(const boost::property_tree::ptree&)>;
  void register_workload(std::string type, builder func);
private:
  std::unordered_map<std::string, builder> _builders;
};