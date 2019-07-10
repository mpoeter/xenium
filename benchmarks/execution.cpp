#include "execution.hpp"

#include <boost/property_tree/ptree.hpp>

#include <iostream>

using boost::property_tree::ptree;

execution::execution(std::uint32_t runtime, std::shared_ptr<benchmark> benchmark) :
  _state(execution_state::initializing),
  _runtime(runtime),
  _benchmark(std::move(benchmark))
{}

execution::~execution() {
  _state.store(execution_state::stopped);
  for (auto& thread : _threads)
    if (thread->_thread.joinable())
      thread->_thread.join();
}

void execution::create_threads(const ptree& config) {
  std::uint32_t count = 0;
  for (auto& it : config)
    count += it.second.get<std::uint32_t>("count", 1);

  _threads.reserve(count);

  for (auto& it : config) {
    auto count = it.second.get<std::uint32_t>("count", 1);
    for (std::uint32_t i = 0; i < count; ++i) {
      // TODO - create different ids for each round
      auto type = it.second.get<std::string>("type", it.first);
      auto thread = _benchmark->create_thread(i, *this, type);
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

  wait_until_all_threads_are_running();

  _state.store(execution_state::running);

  auto start = std::chrono::high_resolution_clock::now();

  std::this_thread::sleep_for(std::chrono::milliseconds(_runtime));

  _state.store(execution_state::stopped);

  wait_until_all_threads_are_finished();

  std::chrono::duration<double, std::milli> runtime = std::chrono::high_resolution_clock::now() - start;
  return build_report(runtime.count());
}

round_report execution::build_report(double runtime) {
  for (auto& thread : _threads)
    thread->_thread.join();
  
  std::vector<thread_report> thread_reports;
  thread_reports.reserve(_threads.size());
  for (unsigned i = 0; i < _threads.size(); ++i) {
    thread_reports.push_back(_threads[i]->report());
  }
  return { thread_reports, runtime };
}

void execution::wait_until_all_threads_are_running() {
  for (auto& thread : _threads)
    wait_until_running(*thread);
}

void execution::wait_until_all_threads_are_finished() {
  for (auto& thread : _threads)
    wait_until_finished(*thread);
}

void execution::wait_until_running(const execution_thread& thread) const {
  wait_until_running_state_is(thread, true);
}

void execution::wait_until_finished(const execution_thread& thread) const {
  wait_until_running_state_is(thread, false);
}

void execution::wait_until_running_state_is(const execution_thread& thread, bool state) const {
  while (thread._is_running.load(std::memory_order_relaxed) != state)
    ;
}

execution_thread::execution_thread(std::uint32_t id, const execution& exec) :
  _execution(exec),
  _id(id),
  _thread(&execution_thread::thread_func, this)
{}

void execution_thread::thread_func() {
  // TODO - track runtime
  wait_until_all_threads_are_started();

  if (_execution.state(std::memory_order_relaxed) == execution_state::stopped)
    return;

  _is_running.store(true);

  try {
    wait_until_benchmark_starts();

    auto start = std::chrono::high_resolution_clock::now();

    while (_execution.state() == execution_state::running)
      run();

    _runtime = std::chrono::high_resolution_clock::now() - start;
  } catch (std::exception& e) {
    std::cout << "Thread " << std::this_thread::get_id() << " failed: " << e.what() << std::endl;
  }
  _is_running.store(false);
}

void execution_thread::setup(const boost::property_tree::ptree& config) {
  auto workload = config.find("workload");
  if (workload == config.not_found())
    return;
  
  workload_factory factory;
  _workload = factory(workload->second);
}

void execution_thread::simulate_workload() {
  if (_workload)
    _workload->simulate();
}

void execution_thread::wait_until_all_threads_are_started() {
  while (_execution.state(std::memory_order_acquire) == execution_state::initializing)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void execution_thread::wait_until_benchmark_starts() {
  while (_execution.state() == execution_state::preparing)
    ;
}
