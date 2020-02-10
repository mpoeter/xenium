#include <iostream>
#include <numeric>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "execution.hpp"

#ifdef WITH_LIBCDS
#include <cds/init.h>
#include <cds/gc/hp.h>
#include <cds/gc/dhp.h>
#endif

using boost::property_tree::ptree;

extern void register_queue_benchmark(registered_benchmarks&);
extern void register_hash_map_benchmark(registered_benchmarks&);

namespace {

struct invalid_argument_exception : std::exception {
  invalid_argument_exception(std::string arg): arg(std::move(arg)) {}
  const char* what() const noexcept override {
    return arg.c_str();
  }
private:
  std::string arg;
};

registered_benchmarks benchmarks;

void print_config(const ptree& config, size_t indent) {
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

void print_config(const ptree& config) {
  print_config(config, 0);
  std::cout << "-----\n";
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

struct options {
  std::string configfile;
  std::string report;
  std::vector<std::string> params;
};

struct key_value {
  std::string key;
  std::string value;
};

key_value split_key_value(const std::string& s) {
  auto pos = s.find("=");
  if (pos == std::string::npos)
    throw invalid_argument_exception(s);
  return {s.substr(0, pos), s.substr(pos + 1)};
}

class runner
{
public:
  runner(const options& opts);
  void run();

private:
  void write_report(const report& report);
  void load_config();
  void warmup();
  report run_benchmark();
  round_report exec_round(std::uint32_t runtime);
  std::shared_ptr<benchmark_builder> find_matching_builder(const benchmark_builders& benchmarks);

  ptree _config;
  std::shared_ptr<benchmark_builder> _builder;
  std::string _reportfile;
  std::uint32_t _current_round = 0;
};

runner::runner(const options& opts) :
  _reportfile(opts.report)
{
  std::ifstream stream(opts.configfile);
  boost::property_tree::json_parser::read_json(stream, _config);
  for (auto& param : opts.params) {
    auto arg = split_key_value(param);
    _config.put(arg.key, arg.value);
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
    throw std::runtime_error("Invalid config");
  }
}

std::shared_ptr<benchmark_builder> runner::find_matching_builder(const benchmark_builders& builders)
{
  auto& ds_config = _config.get_child("ds");
  std::cout << "Given data structure config:\n";
  print_config(ds_config);
  std::vector<std::shared_ptr<benchmark_builder>> matches;
  for(auto& var : builders) {
    auto descriptor = var->get_descriptor();
    if (config_matches(ds_config, descriptor)) {
      matches.push_back(var);
    }
  }

  if (matches.size() == 1)
    return matches[0];

  if (matches.empty()) {
    std::cout << "Could not find a benchmark that matches the given configuration. Available configurations are:\n";
    for (auto& var : builders) {
      print_config(var->get_descriptor());
      std::cout << '\n';
    }
    return nullptr;
  }

  std::cout << "Ambiguous config - found more than one matching benchmark:\n";
  for (auto& var : matches) {
    print_config(var->get_descriptor());
    std::cout << '\n';
  }
  return nullptr;
}

void runner::run() {
  assert(_builder != nullptr);
  warmup();
  auto report = run_benchmark();
  print_summary(report);
  write_report(report);
}

void runner::write_report(const report& report) {
  if (_reportfile.empty())
    return;

  boost::property_tree::ptree ptree;
  try {
    std::ifstream stream(_reportfile);
    if (stream) {
      boost::property_tree::json_parser::read_json(stream, ptree);
    }
  } catch(const boost::property_tree::json_parser_error&) {
    std::cerr << "Failed to parse existing report file \"" << _reportfile << "\"" <<
      " - skipping report generation!" << std::endl;
  }
  
  auto reports = ptree.get_child("reports", {});
  std::ofstream stream(_reportfile);
  reports.push_back(std::make_pair("", report.as_ptree()));
  ptree.put_child("reports", reports);
  boost::property_tree::json_parser::write_json(stream, ptree);
}

void runner::warmup() {
  auto rounds = _config.get<std::uint32_t>("warmup.rounds", 0);
  auto runtime = _config.get<std::uint32_t>("warmup.runtime", 5000);
  for (std::uint32_t i = 0; i < rounds; ++i) {
    std::cout << "warmup round " << i << std::endl;
    auto report = exec_round(runtime);
    (void)report;
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
  ++_current_round;
  auto benchmark = _builder->build();
  benchmark->setup(_config);

  execution exec(_current_round, runtime, benchmark);
  exec.create_threads(_config.get_child("threads"));
  return exec.run();
}

void print_usage() {
  std::cout << "Usage: benchmark" <<
    " --help | <config-file>" <<
    " [--report=<report-file>]" <<
    " [-- <param>=<value> ...]" <<
    std::endl;
}

void print_available_benchmarks() {
  std::cout << "\nAvailable benchmark configurations:\n";
  for (auto& benchmark : benchmarks) {
    std::cout << "=== " << benchmark.first << " ===\n";
    for (auto& config : benchmark.second) 
      print_config(config->get_descriptor());
    std::cout << std::endl;
  }
}

void parse_args(int argc, char* argv[], options& opts) {
  opts.configfile = argv[1];
  for (int i = 2; i < argc; ++i) {
    if (argv[i] == std::string("--")) {
      for (int j = i + 1; j < argc; ++j)
        opts.params.emplace_back(argv[j]);
      return;
    }

    auto arg = split_key_value(argv[i]);
    if (arg.key == "--report")
      opts.report = arg.value;
    else
      throw invalid_argument_exception(argv[i]);
  }
}

}

int main (int argc, char* argv[])
{
  register_queue_benchmark(benchmarks);
  register_hash_map_benchmark(benchmarks);
  
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
    options opts;
    parse_args(argc, argv, opts);

#ifdef WITH_LIBCDS
    cds::Initialize();
    cds::gc::HP hpGC;
    cds::gc::DHP dhpGC;

    cds::threading::Manager::attachThread();
#endif

    runner runner(opts);
    runner.run();

#ifdef WITH_LIBCDS
    cds::Terminate();
#endif

    return 0;
  } catch (const invalid_argument_exception& e) {
    std::cerr << "Invalid argumnent: " << e.what() << std::endl;
    print_usage();
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  }
}
