#pragma once

#include <tao/json/value.hpp>
#include <tao/config/value.hpp>

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
  std::uint64_t operations() const;
  double throughput() const { return operations() / runtime; }

  tao::json::value as_json() const;
};

struct report {
  std::string name;
  std::int64_t timestamp;
  tao::config::value config;
  std::vector<round_report> rounds;

  tao::json::value as_json() const;
};
