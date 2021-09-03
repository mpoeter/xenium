#include <xenium/ramalhete_queue.hpp>
#include <xenium/reclamation/generic_epoch_based.hpp>
#include <xenium/reclamation/hazard_eras.hpp>
#include <xenium/reclamation/hazard_pointer.hpp>
#include <xenium/reclamation/lock_free_ref_count.hpp>
#include <xenium/reclamation/quiescent_state_based.hpp>
#include <xenium/reclamation/stamp_it.hpp>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace {

template <typename Reclaimer>
struct RamalheteQueue : testing::Test {};

int* v1 = new int(42);
int* v2 = new int(43);

using Reclaimers =
  ::testing::Types<xenium::reclamation::lock_free_ref_count<>,
                   xenium::reclamation::hazard_pointer<>::with<
                     xenium::policy::allocation_strategy<xenium::reclamation::hp_allocation::static_strategy<2>>>,
                   xenium::reclamation::hazard_eras<>::with<
                     xenium::policy::allocation_strategy<xenium::reclamation::he_allocation::static_strategy<2>>>,
                   xenium::reclamation::epoch_based<>::with<xenium::policy::scan_frequency<10>>,
                   xenium::reclamation::new_epoch_based<>::with<xenium::policy::scan_frequency<10>>,
                   xenium::reclamation::debra<>::with<xenium::policy::scan_frequency<10>>,
                   xenium::reclamation::quiescent_state_based,
                   xenium::reclamation::stamp_it>;
TYPED_TEST_SUITE(RamalheteQueue, Reclaimers);

TYPED_TEST(RamalheteQueue, push_try_pop_returns_pushed_element) {
  xenium::ramalhete_queue<int*, xenium::policy::reclaimer<TypeParam>> queue;
  queue.push(v1);
  int* elem = nullptr;
  EXPECT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(v1, elem);
}

TYPED_TEST(RamalheteQueue, supports_unique_ptr) {
  xenium::ramalhete_queue<std::unique_ptr<int>, xenium::policy::reclaimer<TypeParam>> queue;
  queue.push(std::make_unique<int>(42));
  std::unique_ptr<int> elem;
  EXPECT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(42, *elem);
}

TYPED_TEST(RamalheteQueue, supports_trivially_copyable_types_smaller_than_a_pointer) {
  {
    xenium::ramalhete_queue<int, xenium::policy::reclaimer<TypeParam>> queue;
    queue.push(42);
    queue.push(-42);
    int elem = 0;
    ASSERT_TRUE(queue.try_pop(elem));
    EXPECT_EQ(42, elem);
    ASSERT_TRUE(queue.try_pop(elem));
    EXPECT_EQ(-42, elem);
  }

  {
    struct dummy {
      char c = 0;
      bool b = false;
      bool operator==(const dummy& rhs) const { return c == rhs.c && b == rhs.b; }
    };
    xenium::ramalhete_queue<dummy, xenium::policy::reclaimer<TypeParam>> queue;
    queue.push({'a', true});
    queue.push({'b', false});
    dummy elem;
    EXPECT_TRUE(queue.try_pop(elem));
    dummy expected = {'a', true};
    EXPECT_EQ(expected, elem);
    EXPECT_TRUE(queue.try_pop(elem));
    expected = {'b', false};
    EXPECT_EQ(expected, elem);
  }
}

TYPED_TEST(RamalheteQueue, deletes_remaining_unique_ptr_entries) {
  unsigned delete_count = 0;
  struct dummy {
    unsigned& delete_count;
    explicit dummy(unsigned& delete_count) : delete_count(delete_count) {}
    ~dummy() { ++delete_count; }
  };
  {
    xenium::ramalhete_queue<std::unique_ptr<dummy>, xenium::policy::reclaimer<TypeParam>> queue;
    queue.push(std::make_unique<dummy>(delete_count));
  }
  EXPECT_EQ(1u, delete_count);
}

TYPED_TEST(RamalheteQueue, push_two_items_pop_them_in_FIFO_order) {
  xenium::ramalhete_queue<int*, xenium::policy::reclaimer<TypeParam>> queue;
  queue.push(v1);
  queue.push(v2);
  int* elem1 = nullptr;
  int* elem2 = nullptr;
  EXPECT_TRUE(queue.try_pop(elem1));
  EXPECT_TRUE(queue.try_pop(elem2));
  EXPECT_EQ(v1, elem1);
  EXPECT_EQ(v2, elem2);
}

TYPED_TEST(RamalheteQueue, parallel_usage) {
  using Reclaimer = TypeParam;
  xenium::ramalhete_queue<int*, xenium::policy::reclaimer<TypeParam>> queue;

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
        queue.push(new int(i));
        int* elem = nullptr;
        EXPECT_TRUE(queue.try_pop(elem));
        EXPECT_TRUE(*elem >= 0 && *elem <= 4);
        delete elem;
      }
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}
} // namespace