#pragma once

#include "benchmark.hpp"
#include "report.hpp"
#include "workload.hpp"

#include <boost/property_tree/ptree_fwd.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <random>
#include <thread>
#include <vector>

enum class execution_state {
  initializing,
  preparing,
  running,
  stopped
};

struct execution;
 
struct execution_thread {
  execution_thread(std::uint32_t id, const execution& exec);
  virtual void setup(const boost::property_tree::ptree& config);
  virtual void run() = 0;
  virtual thread_report report() const { return { boost::property_tree::ptree{}, 0 }; }
private:
  const execution& _execution;
  std::shared_ptr<workload_simulator> _workload; // TODO - make it more flexible
protected:
  void simulate_workload();
  const std::uint32_t _id;
  std::atomic<bool> _is_running{false};
  std::mt19937 _randomizer;
  std::thread _thread;
  std::chrono::duration<double, std::nano> _runtime;
private:
  friend struct execution;
  void thread_func();
  void wait_until_all_threads_are_started();
  void wait_until_benchmark_starts();
};

struct execution {
  execution(std::uint32_t runtime, std::shared_ptr<benchmark> benchmark);
  ~execution();
  void create_threads(const boost::property_tree::ptree& config);
  round_report run();
  execution_state state(std::memory_order order = std::memory_order_relaxed) const;
private:
  void wait_until_all_threads_are_running();
  void wait_until_all_threads_are_finished();

  void wait_until_running(const execution_thread& thread) const;
  void wait_until_finished(const execution_thread& thread) const;
  void wait_until_running_state_is(const execution_thread& thread, bool state) const;
  
  round_report build_report(double runtime);

  std::atomic<execution_state> _state;
  std::uint32_t _runtime;
  std::shared_ptr<benchmark> _benchmark;
  std::vector<std::unique_ptr<execution_thread>> _threads;
};