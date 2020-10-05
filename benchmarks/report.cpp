#include "report.hpp"

std::uint64_t round_report::operations() const {
  std::uint64_t result = 0;
  for (const auto& thread : threads) {
    result += thread.operations;
  }
  return result;
}

tao::json::value round_report::as_json() const {
  tao::json::value result{
    {"runtime", runtime},
    {"operations", operations()},
  };

  tao::json::value thread_data;
  for (const auto& thread : threads) {
    thread_data.push_back(thread.data);
  }

  result.try_emplace("threads", std::move(thread_data));

  return result;
}

tao::json::value report::as_json() const {
  tao::json::value result{
    {"name", name}, {"timestamp", timestamp}, {"config", tao::json::from_string(tao::json::to_string(config))}};

  tao::json::value round_reports;
  for (const auto& round : rounds) {
    round_reports.push_back(round.as_json());
  }

  result.try_emplace("rounds", std::move(round_reports));
  return result;
}