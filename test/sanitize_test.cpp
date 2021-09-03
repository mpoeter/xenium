#include <xenium/detail/port.hpp>

/*
 * These tests are only compiled when ThreadSanitizer is active.
 * They are supposed to verify experimentally (via TSan) that the reclamation schemes
 * ensure the required happens-before relation between the destruction of a guard_ptr
 * to some node and the actual reclamation of that node.
 */

#ifdef __SANITIZE_THREAD__

  #include <xenium/reclamation/generic_epoch_based.hpp>
  #include <xenium/reclamation/hazard_eras.hpp>
  #include <xenium/reclamation/hazard_pointer.hpp>
  #include <xenium/reclamation/lock_free_ref_count.hpp>
  #include <xenium/reclamation/quiescent_state_based.hpp>
  #include <xenium/reclamation/stamp_it.hpp>

  #include <gtest/gtest.h>

  #include <array>
  #include <thread>

namespace {

template <typename Reclaimer>
struct Sanitize : testing::Test {};

using Reclaimers =
  ::testing::Types<xenium::reclamation::lock_free_ref_count<>,
                   xenium::reclamation::hazard_pointer<>::with<
                     xenium::policy::allocation_strategy<xenium::reclamation::hp_allocation::static_strategy<3, 2, 1>>>,
                   xenium::reclamation::hazard_eras<>::with<
                     xenium::policy::allocation_strategy<xenium::reclamation::he_allocation::static_strategy<3, 2, 1>>>,
                   xenium::reclamation::debra<>::with<xenium::policy::scan_frequency<1>>,
                   xenium::reclamation::epoch_based<>::with<xenium::policy::scan_frequency<1>>,
                   xenium::reclamation::new_epoch_based<>::with<xenium::policy::scan_frequency<1>>,
                   xenium::reclamation::quiescent_state_based,
                   xenium::reclamation::stamp_it>;
TYPED_TEST_SUITE(Sanitize, Reclaimers);

  #ifdef DEBUG
const int MaxIterations = 1000;
  #else
const int MaxIterations = 10000;
  #endif

TYPED_TEST(Sanitize, guard_ptrs) {
  struct node : TypeParam::template enable_concurrent_ptr<node, 1> {
    int dummy;
  };

  for (int x = 0; x < 10; ++x) {
    using concurrent_ptr = typename TypeParam::template concurrent_ptr<node, 1>;
    using guard_ptr = typename concurrent_ptr::guard_ptr;

    constexpr int NumPtrs = 10;
    std::array<concurrent_ptr, NumPtrs> ptrs;

    std::thread t1([&ptrs]() {
      node* n = new node();
      for (int i = 0; i < MaxIterations; ++i) {
        guard_ptr guard;
        for (int j = 0; j < NumPtrs; ++j) {
          if (ptrs[j].load(std::memory_order_relaxed) == nullptr) {
            ptrs[j].store(n, std::memory_order_release);
            n = new node();
          }
        }
      }
    });

    std::thread t2([&ptrs]() {
      for (int i = 0; i < MaxIterations; ++i) {
        guard_ptr guard;
        for (int j = 0; j < NumPtrs; ++j) {
          guard.acquire(ptrs[j], std::memory_order_acquire);
          if (guard) {
            guard->dummy = 42;
          }
        }
      }
    });

    std::thread t3([&ptrs]() {
      for (int i = 0; i < MaxIterations; ++i) {
        guard_ptr guard;
        for (int j = 0; j < NumPtrs; ++j) {
          guard.acquire(ptrs[j], std::memory_order_acquire);
          if (guard) {
            ptrs[j].store(nullptr, std::memory_order_relaxed);
            guard_ptr copy = guard;
            copy.reclaim();
          }
        }
      }
    });

    t1.join();
    t2.join();
    t3.join();
  }
}

} // namespace
#endif
