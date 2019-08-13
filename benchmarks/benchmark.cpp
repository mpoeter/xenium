#include "benchmark.hpp"
#include "execution.hpp"

#include <boost/property_tree/ptree.hpp>

using boost::property_tree::ptree;

namespace config {
  void prefill::setup(const ptree& config, std::uint32_t default_count) {
    count = default_count;
    auto node = config.find("prefill");
    if (node != config.not_found()) {
      if (node->second.empty()) {
        count = node->second.get_value<std::uint32_t>();
      } else {
        serial = node->second.get<bool>("serial", false);
        count = node->second.get<std::uint32_t>("count", count);
      }
    }
  }

  std::uint32_t prefill::get_thread_quota(std::uint32_t thread_id, std::uint32_t num_threads) {
    thread_id &= execution::thread_id_mask;
    std::uint32_t result = 0;
    if (serial) {
      if (thread_id == 0)
        result = count;
    } else {
      result = count / num_threads;
      if (thread_id < count % num_threads)
        ++result;
    }
    return result;
  }
}