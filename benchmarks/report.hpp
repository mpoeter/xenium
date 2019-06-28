#pragma once

#include <boost/property_tree/ptree.hpp>

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
  // a property tree containing arbitrary result data for this thread
  boost::property_tree::ptree data;
  // total number of operations performed by this thread
  std::uint64_t operations;
};

struct round_report {
  std::vector<thread_report> threads;
  double runtime; // runtime in milliseconds
  std::uint64_t operations() const;
  double throughput() const { return operations() / runtime; }

  boost::property_tree::ptree as_ptree() const;
};

struct report {
  std::string name;
  std::int64_t timestamp;
  boost::property_tree::ptree config;
  std::vector<round_report> rounds;

  boost::property_tree::ptree as_ptree() const;
};
