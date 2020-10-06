#include "benchmark.hpp"
#include "execution.hpp"

#include <tao/config/value.hpp>

using config_t = tao::config::value;

namespace config {
void prefill::setup(const config_t& config, std::uint64_t default_count) {
  count = default_count;
  const auto* node = config.find("prefill");
  if (node != nullptr) {
    if (node->is_integer()) {
      count = node->get_unsigned();
    } else {
      serial = node->optional<bool>("serial").value_or(false);
      count = node->optional<std::uint64_t>("count").value_or(count);
    }
  }
}

std::uint64_t prefill::get_thread_quota(std::uint32_t thread_id, std::uint32_t num_threads) const {
  thread_id &= execution::thread_id_mask;
  std::uint64_t result = 0;
  if (serial) {
    if (thread_id == 0) {
      result = count;
    }
  } else {
    result = count / num_threads;
    if (thread_id < count % num_threads) {
      ++result;
    }
  }
  return result;
}
} // namespace config