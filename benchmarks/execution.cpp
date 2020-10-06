#include "execution.hpp"

#include <tao/config/value.hpp>

#ifdef WITH_LIBCDS
  #include <cds/gc/hp.h>
#endif

#include <iostream>

using config_t = tao::config::value;

execution::execution(std::uint32_t round, std::uint32_t runtime, std::shared_ptr<benchmark> benchmark) :
    _state(execution_state::starting),
    _round(round),
    _runtime(runtime),
    _benchmark(std::move(benchmark)) {}

execution::~execution() {
  _state.store(execution_state::stopped);
  for (auto& thread : _threads) {
    if (thread->_thread.joinable()) {
      thread->_thread.join();
    }
  }
}

void execution::create_threads(const config_t& config) {
  std::uint32_t total_count = 0;
  for (const auto& it : config.get_object()) {
    total_count += it.second.optional<std::uint32_t>("count").value_or(1);
  }

  _threads.reserve(total_count);

  std::uint32_t cnt = 0;
  for (const auto& it : config.get_object()) {
    auto count = it.second.optional<std::uint32_t>("count").value_or(1);
    for (std::uint32_t i = 0; i < count; ++i, ++cnt) {
      auto type = it.second.optional<std::string>("type").value_or(it.first);
      auto id = (_round << thread_id_bits) | cnt;
      auto thread = _benchmark->create_thread(id, *this, type);
      _threads.push_back(std::move(thread));
      _threads.back()->setup(it.second);
    }
  }
}

execution_state execution::state(std::memory_order order) const {
  return _state.load(order);
}

round_report execution::run() {
  _state.store(execution_state::preparing);

  wait_until_all_threads_are(thread_state::running);

  _state.store(execution_state::initializing);

  wait_until_all_threads_are(thread_state::ready);

  _state.store(execution_state::running);

  auto start = std::chrono::high_resolution_clock::now();

  std::this_thread::sleep_for(std::chrono::milliseconds(_runtime));

  _state.store(execution_state::stopped);

  wait_until_all_threads_are(thread_state::finished);

  std::chrono::duration<double, std::milli> runtime = std::chrono::high_resolution_clock::now() - start;
  return build_report(runtime.count());
}

round_report execution::build_report(double runtime) {
  for (auto& thread : _threads) {
    thread->_thread.join();
  }

  std::vector<thread_report> thread_reports;
  thread_reports.reserve(_threads.size());
  for (auto& thread : _threads) {
    thread_reports.push_back(thread->report());
  }
  return {thread_reports, runtime};
}

void execution::wait_until_all_threads_are(thread_state state) {
  for (auto& thread : _threads) {
    wait_until_thread_state_is(*thread, state);
  }
}

void execution::wait_until_thread_state_is(const execution_thread& thread, thread_state expected) {
  auto state = thread._state.load(std::memory_order_relaxed);
  while (state != expected) {
    if (state == thread_state::finished) {
      throw std::runtime_error("worker thread finished prematurely");
    }
    state = thread._state.load(std::memory_order_relaxed);
  }
}

execution_thread::execution_thread(std::uint32_t id, const execution& exec) :
    _execution(exec),
    _id(id),
    _thread(&execution_thread::thread_func, this) {}

void execution_thread::thread_func() {
#ifdef WITH_LIBCDS
  cds::threading::Manager::attachThread();
#endif

  wait_until_all_threads_are_started();
  try {
    do_run();
  } catch (std::exception& e) {
    std::cout << "Thread " << std::this_thread::get_id() << " failed: " << e.what() << std::endl;
  }
  _state.store(thread_state::finished);

#ifdef WITH_LIBCDS
  cds::threading::Manager::detachThread();
#endif
}

void execution_thread::do_run() {
  if (_execution.state(std::memory_order_relaxed) == execution_state::stopped) {
    return;
  }

  _state.store(thread_state::running);

  wait_until_initialization();

  initialize(_execution.num_threads());

  _state.store(thread_state::ready);

  wait_until_benchmark_starts();

  auto start = std::chrono::high_resolution_clock::now();

  while (_execution.state() == execution_state::running) {
    run();
  }

  _runtime = std::chrono::high_resolution_clock::now() - start;
}

void execution_thread::setup(const config_t& config) {
  const auto* workload = config.find("workload");
  if (workload == nullptr) {
    return;
  }

  workload_factory factory;
  _workload = factory(*workload);
}

void execution_thread::simulate_workload() {
  if (_workload) {
    _workload->simulate();
  }
}

void execution_thread::wait_until_all_threads_are_started() {
  while (_execution.state(std::memory_order_acquire) == execution_state::starting) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

void execution_thread::wait_until_initialization() {
  while (_execution.state() == execution_state::preparing) {
    ;
  }
}

void execution_thread::wait_until_benchmark_starts() {
  while (_execution.state() == execution_state::initializing) {
    ;
  }
}