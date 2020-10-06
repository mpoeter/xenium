#pragma once

#include <tao/config/value.hpp>
#include <tao/json/value.hpp>

#include <string>
#include <vector>

/*
  {
    name: "bla",
    timestamp: "<epoch>",
    config: {...},
    rounds: [
      {
        threads: {} | []
        runtime: 10054.736,
        operations: 87324

      }
    ]
  }
*/

struct thread_report {
  // a JSON value containing arbitrary result data for this thread
  tao::json::value data;
  // total number of operations performed by this thread
  std::uint64_t operations;
};

struct round_report {
  std::vector<thread_report> threads;
  double runtime; // runtime in milliseconds
  [[nodiscard]] std::uint64_t operations() const;
  [[nodiscard]] double throughput() const { return static_cast<double>(operations()) / runtime; }

  [[nodiscard]] tao::json::value as_json() const;
};

struct report {
  std::string name;
  std::int64_t timestamp;
  tao::config::value config;
  std::vector<round_report> rounds;

  [[nodiscard]] tao::json::value as_json() const;
};
