#include "report.hpp"

std::uint64_t round_report::operations() const {
  std::uint64_t result = 0;
  for (const auto& thread : threads) {
    result += thread.operations;
  }
  return result;
}

boost::property_tree::ptree round_report::as_ptree() const {
  boost::property_tree::ptree result;
  boost::property_tree::ptree thread_data;
  for (const auto& thread : threads) {
    thread_data.push_back(std::make_pair("", thread.data));
  }

  result.put("runtime", runtime);
  result.put("operations", operations());
  result.put_child("threads", thread_data);
  
  return result;
}

boost::property_tree::ptree report::as_ptree() const {
  boost::property_tree::ptree result;
  result.put("name", name);
  result.put("timestamp", timestamp);
  result.put_child("config", config);

  boost::property_tree::ptree round_reports;
  for (const auto& round : rounds)
    round_reports.push_back(std::make_pair("", round.as_ptree()));

  result.put_child("rounds", round_reports);
  return result;
}