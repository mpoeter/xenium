#pragma once

#include "benchmark.hpp"
#include "report.hpp"
#include "workload.hpp"

#include <tao/config/value.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <random>
#include <thread>
#include <vector>

enum class execution_state { starting, preparing, initializing, running, stopped };

enum class thread_state { starting, running, ready, finished };

struct initialization_failure : std::exception {
  [[nodiscard]] const char* what() const noexcept override { return "Failed to initialize data structure under test-"; }
};

struct execution;

struct execution_thread {
  execution_thread(std::uint32_t id, const execution& exec);
  virtual ~execution_thread() = default;
  virtual void setup(const tao::config::value& config);
  virtual void run() = 0;
  virtual void initialize(std::uint32_t /*num_threads*/) {}
  [[nodiscard]] virtual thread_report report() const { return {{}, 0}; }
  [[nodiscard]] std::uint32_t id() const { return _id; }

private:
  const execution& _execution;
  std::shared_ptr<workload_simulator> _workload;

protected:
  void simulate_workload();
  const std::uint32_t _id;
  std::atomic<thread_state> _state{thread_state::starting};
  std::mt19937_64 _randomizer{};
  std::thread _thread{};
  std::chrono::duration<double, std::milli> _runtime{};

private:
  friend struct execution;
  void thread_func();
  void do_run();
  void wait_until_all_threads_are_started();
  void wait_until_initialization();
  void wait_until_benchmark_starts();
};

struct execution {
  static constexpr std::uint32_t thread_id_bits = 16;
  static constexpr std::uint32_t thread_id_mask = (1 << thread_id_bits) - 1;

  execution(std::uint32_t round, std::uint32_t runtime, std::shared_ptr<benchmark> benchmark);
  ~execution();
  void create_threads(const tao::config::value& config);
  round_report run();
  [[nodiscard]] execution_state state(std::memory_order order = std::memory_order_relaxed) const;
  [[nodiscard]] std::uint32_t num_threads() const { return static_cast<std::uint32_t>(_threads.size()); }

private:
  void wait_until_all_threads_are(thread_state state);

  void wait_until_running(const execution_thread& thread) const;
  void wait_until_finished(const execution_thread& thread) const;
  static void wait_until_thread_state_is(const execution_thread& thread, thread_state expected) ;

  round_report build_report(double runtime);

  std::atomic<execution_state> _state;
  std::uint32_t _round;
  std::uint32_t _runtime;
  std::shared_ptr<benchmark> _benchmark;
  std::vector<std::unique_ptr<execution_thread>> _threads;
};
