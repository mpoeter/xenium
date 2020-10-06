#include <fstream>
#include <iostream>
#include <numeric>

#ifdef _MSC_VER
  // taocpp config internally uses 128-bit arithmetic to check for overflows,
  // but this is not supported by MSVC -> revert to 64-bit.
  #define __int128_t std::int64_t
#endif

#include <tao/json/from_stream.hpp>

#include <tao/config/schema.hpp>
#include <tao/config/value.hpp>

#include <tao/config/internal/configurator.hpp>

#include "execution.hpp"

#ifdef WITH_LIBCDS
  #include <cds/gc/dhp.h>
  #include <cds/gc/hp.h>
  #include <cds/init.h>
#endif

extern void register_queue_benchmark(registered_benchmarks&);
extern void register_hash_map_benchmark(registered_benchmarks&);

namespace {

struct invalid_argument_exception : std::exception {
  explicit invalid_argument_exception(std::string arg) : arg(std::move(arg)) {}
  [[nodiscard]] const char* what() const noexcept override { return arg.c_str(); }

private:
  std::string arg;
};

registered_benchmarks benchmarks;

template <template <typename...> class Traits>
void print_config(const tao::json::basic_value<Traits>& config) {
  std::cout << tao::json::to_string(config, 2) << std::endl;
}

void print_summary(const report& report) {
  std::vector<double> throughput;
  throughput.reserve(report.rounds.size());
  for (const auto& round : report.rounds) {
    throughput.push_back(round.throughput());
  }

  auto min = *std::min_element(throughput.begin(), throughput.end());
  auto max = *std::max_element(throughput.begin(), throughput.end());
  auto cnt = static_cast<double>(throughput.size());
  auto avg = std::accumulate(throughput.begin(), throughput.end(), 0.0) / cnt;
  auto sqr = [](double v) { return v * v; };
  double var = 0;
  for (auto v : throughput) {
    var += sqr(v - avg);
  }
  var /= cnt;

  std::cout << "Summary:\n"
            << "  min: " << min << " ops/ms\n"
            << "  max: " << max << " ops/ms\n"
            << "  avg: " << avg << " ops/ms\n"
            << "  stddev: " << sqrt(var) << std::endl;
}

bool configs_match(const tao::config::value& config, const tao::json::value& descriptor);

bool objects_match(const tao::config::value::object_t& config, const tao::json::value::object_t& descriptor) {
  return std::all_of(config.begin(), config.end(), [&](const auto& entry) {
    auto it = descriptor.find(entry.first);
    if (it == descriptor.end()) {
      return false;
    }

    if (it->second.is_string() && it->second.get_string() == DYNAMIC_PARAM) {
      return true;
    }

    return configs_match(entry.second, it->second);
  });
}

bool scalars_match(const tao::config::value& config, const tao::json::value& descriptor) {
  if (config.is_string() != descriptor.is_string() || config.is_integer() != descriptor.is_integer() ||
      config.is_boolean() != descriptor.is_boolean()) {
    return false;
  }

  if (config.is_integer()) {
    return config.as<std::int64_t>() == descriptor.as<std::int64_t>();
  }

  if (config.is_string()) {
    return config.get_string() == descriptor.get_string();
  }

  if (config.is_boolean()) {
    return config.get_boolean() == descriptor.get_boolean();
  }

  throw std::runtime_error("Found unexpected type in config.");
}

bool configs_match(const tao::config::value& config, const tao::json::value& descriptor) {
  if (config.is_object() != descriptor.is_object()) {
    return false;
  }

  if (config.is_object()) {
    return objects_match(config.get_object(), descriptor.get_object());
  }

  return scalars_match(config, descriptor);
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
  if (pos == std::string::npos) {
    throw invalid_argument_exception(s);
  }
  return {s.substr(0, pos), s.substr(pos + 1)};
}

class runner {
public:
  explicit runner(const options& opts);
  void run();

private:
  void write_report(const report& report);
  void load_config();
  void warmup();
  report run_benchmark();
  round_report exec_round(std::uint32_t runtime);
  std::shared_ptr<benchmark_builder> find_matching_builder(const benchmark_builders& builders);

  tao::config::value _config;
  std::shared_ptr<benchmark_builder> _builder;
  std::string _reportfile;
  std::uint32_t _current_round = 0;
};

runner::runner(const options& opts) : _reportfile(opts.report) {
  tao::config::internal::configurator configurator;
  configurator.parse(tao::config::pegtl::file_input(opts.configfile));

  for (const auto& param : opts.params) {
    // TODO - error handling
    std::cout << "param: " << param << std::endl;
    configurator.parse(tao::config::pegtl_input_t(param, "command line param"));
  }

  _config = configurator.process<tao::config::traits>(tao::config::schema::builtin());

  load_config();
}

void runner::load_config() {
  auto type = _config.as<std::string>("type");
  auto it = benchmarks.find(type);
  if (it == benchmarks.end()) {
    throw std::runtime_error("Invalid benchmark type " + type);
  }

  _builder = find_matching_builder(it->second);
  if (!_builder) {
    throw std::runtime_error("Invalid config");
  }
}

std::shared_ptr<benchmark_builder> runner::find_matching_builder(const benchmark_builders& builders) {
  auto& ds_config = _config["ds"];
  std::cout << "Given data structure config:\n";
  print_config(ds_config);
  std::vector<std::shared_ptr<benchmark_builder>> matches;
  for (const auto& var : builders) {
    auto descriptor = var->get_descriptor();
    if (configs_match(ds_config, descriptor)) {
      matches.push_back(var);
    }
  }

  if (matches.size() == 1) {
    return matches[0];
  }

  if (matches.empty()) {
    std::cout << "Could not find a benchmark that matches the given configuration. Available configurations are:\n";
    for (const auto& var : builders) {
      print_config(var->get_descriptor());
      std::cout << "---\n";
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
  if (_reportfile.empty()) {
    return;
  }

  tao::json::value json;
  try {
    std::ifstream stream(_reportfile);
    if (stream) {
      json = tao::json::from_stream(stream, _reportfile);
    }
  } catch (...) {
    std::cerr << "Failed to parse existing report file \"" << _reportfile << "\""
              << " - skipping report generation!" << std::endl;
  }

  auto& reports = json["reports"];
  std::ofstream ostream(_reportfile);
  reports.push_back(report.as_json());
  tao::json::to_stream(ostream, json, 2);
}

void runner::warmup() {
  auto rounds = _config.optional<std::uint32_t>("warmup.rounds").value_or(0);
  auto runtime = _config.optional<std::uint32_t>("warmup.runtime").value_or(5000);
  for (std::uint32_t i = 0; i < rounds; ++i) {
    std::cout << "warmup round " << i << std::endl;
    auto report = exec_round(runtime);
    (void)report;
  }
}

report runner::run_benchmark() {
  auto rounds = _config.optional<std::uint32_t>("rounds").value_or(10);
  auto runtime = _config.optional<std::uint32_t>("runtime").value_or(10000);
  auto timestamp =
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());

  std::vector<round_report> round_reports;
  round_reports.reserve(rounds);
  for (std::uint32_t i = 0; i < rounds; ++i) {
    std::cout << "round " << i << std::flush;
    auto report = exec_round(runtime);
    std::cout << " - " << static_cast<double>(report.operations()) / report.runtime << " ops/ms" << std::endl;
    round_reports.push_back(std::move(report));
  }

  return {_config.optional<std::string>("name").value_or(_config.as<std::string>("type")),
          timestamp.count(),
          _config,
          round_reports};
}

round_report runner::exec_round(std::uint32_t runtime) {
  ++_current_round;
  auto benchmark = _builder->build();
  benchmark->setup(_config);

  execution exec(_current_round, runtime, benchmark);
  exec.create_threads(_config["threads"]);
  return exec.run();
}

void print_usage() {
  std::cout << "Usage: benchmark"
            << " --help | <config-file>"
            << " [--report=<report-file>]"
            << " [-- <param>=<value> ...]" << std::endl;
}

void print_available_benchmarks() {
  std::cout << "\nAvailable benchmark configurations:\n";
  for (auto& benchmark : benchmarks) {
    std::cout << "=== " << benchmark.first << " ===\n";
    for (auto& config : benchmark.second) {
      print_config(config->get_descriptor());
      std::cout << "---\n";
    }
    std::cout << std::endl;
  }
}

void parse_args(int argc, char* argv[], options& opts) {
  opts.configfile = argv[1];
  for (int i = 2; i < argc; ++i) {
    if (argv[i] == std::string("--")) {
      for (int j = i + 1; j < argc; ++j) {
        opts.params.emplace_back(argv[j]);
      }
      return;
    }

    auto arg = split_key_value(argv[i]);
    if (arg.key == "--report") {
      opts.report = arg.value;
    } else {
      throw invalid_argument_exception(argv[i]);
    }
  }
}

} // namespace

int main(int argc, char* argv[]) {
  register_queue_benchmark(benchmarks);
  register_hash_map_benchmark(benchmarks);

#if !defined(NDEBUG)
  std::cout << "==============================\n"
            << "  This is a __DEBUG__ build!  \n"
            << "==============================" << std::endl;
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

  try {
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
