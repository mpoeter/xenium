#pragma once

#include <cstdint>

namespace citissime { namespace reclamation { namespace detail {
#ifdef WITH_PERF_COUNTER
  struct perf_counter
  {
    perf_counter(std::size_t& counter) : counter(counter), cnt() {}
    ~perf_counter() { counter += cnt;  }
    void inc() { ++cnt; }
  private:
    std::size_t& counter;
    std::size_t cnt;
  };

  #define PERF_COUNTER(name, counter) citissime::reclamation::detail::perf_counter name(counter);
  #define INC_PERF_CNT(counter) ++counter;
#else
  struct perf_counter
  {
    perf_counter() {}
    void inc() {}
  };

  #define PERF_COUNTER(name, counter) citissime::reclamation::detail::perf_counter name;
  #define INC_PERF_CNT(counter)
#endif
}}}
