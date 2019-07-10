#include <iostream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "execution.hpp"

using boost::property_tree::ptree;
std::unordered_map<std::string, benchmark_builders> benchmarks;

namespace {

void print_config(const ptree& config, int indent = 0) {
  if (config.empty()) {
    std::cout << config.get_value<std::string>() << '\n';
  } else {
    for (auto& it : config) {
      std::cout << std::string(2 * (indent + 1), ' ');
      std::cout << it.first << ": ";
      if (!it.second.empty())
        std::cout << '\n';
      print_config(it.second, indent + 1);
    }
  }
}

double sqr(double v) {
  return v*v;
}

void print_summary(const report& report) {
  std::vector<double> throughput;
  throughput.reserve(report.rounds.size());
  for (const auto& round : report.rounds)
    throughput.push_back(round.throughput());

  auto min = *std::min_element(throughput.begin(), throughput.end());
  auto max = *std::max_element(throughput.begin(), throughput.end());
  auto avg = std::accumulate(throughput.begin(), throughput.end(), 0.0) / throughput.size();
  double var = 0;
  for (auto v : throughput) {
    var += sqr(v - avg);  
  }
  var /= throughput.size();
  

  std::cout << "Summary:\n" <<
    "  min: " << min << " ops/ms\n" <<
    "  max: " << max << " ops/ms\n" <<
    "  avg: " << avg << " ops/ms\n" <<
    "  stddev: " << sqrt(var) << std::endl;
}

bool config_matches(const ptree& config, const ptree& descriptor) {
  for (auto& entry : config) {
    auto it = descriptor.find(entry.first);
    if (it == descriptor.not_found()) {
      return false;
    }
    
    if (entry.second.empty()) {
      if (it->second.get_value<std::string>() == DYNAMIC_PARAM)
        continue;

      if (entry.second != it->second)
        return false;
    } else if (!config_matches(entry.second, it->second))
      return false;
  }
  return true;
}

class runner
{
public:
  runner(char** argv, int argc);
  void run();

private:
  void load_config();
  void warmup();
  report run_benchmark();
  round_report exec_round(std::uint32_t runtime);
  std::shared_ptr<benchmark_builder> find_matching_builder(const benchmark_builders& benchmarks);

  ptree _config;
  std::shared_ptr<benchmark_builder> _builder;
};

runner::runner(char** argv, int argc) {
  std::ifstream stream(argv[1]);
  boost::property_tree::json_parser::read_json(stream, _config);
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    auto pos = arg.find("=");
    if (pos == std::string::npos)
      throw std::runtime_error("Invalid argument: " + arg);
    auto key = arg.substr(0, pos);
    auto val = arg.substr(pos + 1);
    _config.put(key, val);
  }
  
  load_config();
}

void runner::load_config() {
  auto type = _config.get<std::string>("type");
  auto it = benchmarks.find(type);
  if (it == benchmarks.end())
    throw std::runtime_error("Invalid benchmark type " + type);
  
  _builder = find_matching_builder(it->second);
  if (!_builder) {
    std::cout << "Could not find a benchmark that matches the given configuration. Available configurations are:\n";
    for (auto& var : it->second) {
      print_config(var->get_descriptor());
      std::cout << '\n';
    }
    throw std::runtime_error("Invalid config");
  }
}

std::shared_ptr<benchmark_builder> runner::find_matching_builder(const benchmark_builders& benchmarks)
{
  auto& ds_config = _config.get_child("ds");
  std::cout << "Given data structure config:\n";
  print_config(ds_config);
  for(auto& var : benchmarks) {
    auto descriptor = var->get_descriptor();
    if (config_matches(ds_config, descriptor)) {
      std::cout << "Found matching benchmark:\n";
      print_config(descriptor);
      return var;
    }
  }
  return nullptr;
}

void runner::run() {
  assert(_builder != nullptr);
  warmup();
  auto report = run_benchmark();
  print_summary(report);
  // TODO - write report to file
}

void runner::warmup() {
  auto rounds = _config.get<std::uint32_t>("warmup.rounds", 0);
  auto runtime = _config.get<std::uint32_t>("warmup.runtime", 5000);
  for (std::uint32_t i = 0; i < rounds; ++i) {
    std::cout << "warmup round " << i << std::endl;
    exec_round(runtime);
  }
}

report runner::run_benchmark() {
  auto rounds = _config.get<std::uint32_t>("rounds", 10);
  auto runtime = _config.get<std::uint32_t>("runtime", 10000);
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch());

  std::vector<round_report> round_reports;
  round_reports.reserve(rounds);
  for (std::uint32_t i = 0; i < rounds; ++i) {
    std::cout << "round " << i << std::flush;
    auto report = exec_round(runtime);
    std::cout << " - " << report.operations() / report.runtime << " ops/ms" << std::endl;
    round_reports.push_back(std::move(report));
  }

  return {
    _config.get<std::string>("name", _config.get<std::string>("type")),
    timestamp.count(),
    _config,
    round_reports
  };
}

round_report runner::exec_round(std::uint32_t runtime) {
  auto benchmark = _builder->build();
  benchmark->setup(_config);

  execution exec(runtime, benchmark);
  exec.create_threads(_config.get_child("threads"));
  return exec.run();
}

void print_usage() {
  std::cout << "Usage: benchmark --help | <config-file> [<param>=<value> ...]" << std::endl;
}

void print_available_benchmarks() {
  std::cout << "\nAvailable benchmark configurations:\n";
  for (auto& benchmark : benchmarks) {
    std::cout << "=== " << benchmark.first << " ===\n";
    for (auto& config : benchmark.second) 
      print_config(config->get_descriptor());
    std::cout << std::endl;
  }
  // TODO - improve output
}

}

int main (int argc, char* argv[])
{
#if !defined(NDEBUG)
  std::cout
    << "==============================\n"
    << "  This is a __DEBUG__ build!  \n"
    << "=============================="
    << std::endl;
#endif

  if (argc < 2) {
    print_usage();
    return 1;
  }

  if (argv[1] == std::string("--help")) {
    print_usage();
    print_available_benchmarks();
    return 0;
  }

  try
  {
    runner runner(argv, argc);
    runner.run();
    return 0;
  } catch (const std::exception& e) {
    std::cout << "ERROR: " << e.what() << std::endl;
    return 1;
  }
}
