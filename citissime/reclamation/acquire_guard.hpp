#ifndef CITISSIME_ACQUIRE_GUARD_HPP
#define CITISSIME_ACQUIRE_GUARD_HPP

#include <atomic>

namespace citissime {

template <typename ConcurrentPtr>
auto acquire_guard(ConcurrentPtr& p, std::memory_order order = std::memory_order_seq_cst)
{
  typename ConcurrentPtr::guard_ptr guard;
  guard.acquire(p, order);
  return guard;
}

}

#endif
