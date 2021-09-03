#include <xenium/kirsch_kfifo_queue.hpp>
#include <xenium/reclamation/generic_epoch_based.hpp>
#include <xenium/reclamation/hazard_eras.hpp>
#include <xenium/reclamation/hazard_pointer.hpp>
#include <xenium/reclamation/quiescent_state_based.hpp>
#include <xenium/reclamation/stamp_it.hpp>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace {

template <typename Reclaimer>
struct KirschKFifoQueue : testing::Test {};

int* v1 = new int(42);
int* v2 = new int(43);

using Reclaimers =
  ::testing::Types<xenium::reclamation::hazard_pointer<>::with<
                     xenium::policy::allocation_strategy<xenium::reclamation::hp_allocation::static_strategy<2>>>,
                   xenium::reclamation::hazard_eras<>::with<
                     xenium::policy::allocation_strategy<xenium::reclamation::he_allocation::static_strategy<2>>>,
                   xenium::reclamation::quiescent_state_based,
                   xenium::reclamation::debra<>::with<xenium::policy::scan_frequency<10>>,
                   xenium::reclamation::epoch_based<>::with<xenium::policy::scan_frequency<10>>,
                   xenium::reclamation::new_epoch_based<>::with<xenium::policy::scan_frequency<10>>,
                   xenium::reclamation::stamp_it>;
TYPED_TEST_SUITE(KirschKFifoQueue, Reclaimers);

TYPED_TEST(KirschKFifoQueue, push_try_pop_returns_pushed_element) {
  xenium::kirsch_kfifo_queue<int*, xenium::policy::reclaimer<TypeParam>> queue(1);
  queue.push(v1);
  int* elem = nullptr;
  ASSERT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(v1, elem);
}

TYPED_TEST(KirschKFifoQueue, supports_unique_ptr) {
  xenium::kirsch_kfifo_queue<std::unique_ptr<int>, xenium::policy::reclaimer<TypeParam>> queue(1);
  auto elem = std::make_unique<int>(42);
  auto* p = elem.get();
  queue.push(std::move(elem));
  ASSERT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(p, elem.get()); // NOLINT (use-after-move)
  EXPECT_EQ(42, *elem); // NOLINT (use-after-move)
}

TYPED_TEST(KirschKFifoQueue, deletes_remaining_unique_ptr_entries) {
  unsigned delete_count = 0;
  struct dummy {
    unsigned& delete_count;
    explicit dummy(unsigned& delete_count) : delete_count(delete_count) {}
    ~dummy() { ++delete_count; }
  };
  {
    xenium::kirsch_kfifo_queue<std::unique_ptr<dummy>, xenium::policy::reclaimer<TypeParam>> queue(1);
    for (int i = 0; i < 200; ++i) {
      queue.push(std::make_unique<dummy>(delete_count));
    }
  }
  EXPECT_EQ(200u, delete_count);
}

TYPED_TEST(KirschKFifoQueue, push_two_items_pop_them_in_FIFO_order) {
  xenium::kirsch_kfifo_queue<int*, xenium::policy::reclaimer<TypeParam>> queue(1);
  queue.push(v1);
  queue.push(v2);
  int* elem1 = nullptr;
  int* elem2 = nullptr;
  EXPECT_TRUE(queue.try_pop(elem1));
  EXPECT_TRUE(queue.try_pop(elem2));
  EXPECT_EQ(v1, elem1);
  EXPECT_EQ(v2, elem2);
}

TYPED_TEST(KirschKFifoQueue, push_large_number_of_entries_pop_them_in_FIFO_order) {
  [[maybe_unused]] typename TypeParam::region_guard guard{};
  xenium::kirsch_kfifo_queue<int*, xenium::policy::reclaimer<TypeParam>> queue(1);
  for (int i = 0; i < 1000; ++i) {
    queue.push(new int(i));
  }

  int* elem = nullptr;
  for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(queue.try_pop(elem));
    EXPECT_EQ(i, *elem);
    delete elem;
  }
}

TYPED_TEST(KirschKFifoQueue, parallel_usage) {
  using Reclaimer = TypeParam;
  xenium::kirsch_kfifo_queue<int*, xenium::policy::reclaimer<TypeParam>> queue(8);

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.push_back(std::thread([i, &queue] {
#ifdef DEBUG
      const int MaxIterations = 1000;
#else
      const int MaxIterations = 10000;
#endif
      for (int j = 0; j < MaxIterations; ++j) {
        [[maybe_unused]] typename Reclaimer::region_guard guard{};
        for (int k = 0; k < 10; ++k) {
          queue.push(new int(i));
          int* elem = nullptr;
          ASSERT_TRUE(queue.try_pop(elem));
          ASSERT_NE(nullptr, elem);
          EXPECT_TRUE(*elem >= 0 && *elem <= 4);
          delete elem;
        }
      }
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}
} // namespace