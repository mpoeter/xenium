#include <xenium/michael_scott_queue.hpp>
#include <xenium/reclamation/generic_epoch_based.hpp>
#include <xenium/reclamation/hazard_eras.hpp>
#include <xenium/reclamation/hazard_pointer.hpp>
#include <xenium/reclamation/lock_free_ref_count.hpp>
#include <xenium/reclamation/quiescent_state_based.hpp>
#include <xenium/reclamation/stamp_it.hpp>

#include "helpers.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace {

template <typename Reclaimer>
struct MichaelScottQueue : testing::Test {};

using Reclaimers =
  ::testing::Types<xenium::reclamation::lock_free_ref_count<>,
                   xenium::reclamation::hazard_pointer<>::with<
                     xenium::policy::allocation_strategy<xenium::reclamation::hp_allocation::static_strategy<2>>>,
                   xenium::reclamation::hazard_eras<>::with<
                     xenium::policy::allocation_strategy<xenium::reclamation::he_allocation::static_strategy<2>>>,
                   xenium::reclamation::quiescent_state_based,
                   xenium::reclamation::stamp_it,
                   xenium::reclamation::epoch_based<>::with<xenium::policy::scan_frequency<10>>,
                   xenium::reclamation::new_epoch_based<>::with<xenium::policy::scan_frequency<10>>,
                   xenium::reclamation::debra<>::with<xenium::policy::scan_frequency<10>>>;
TYPED_TEST_SUITE(MichaelScottQueue, Reclaimers);

TYPED_TEST(MichaelScottQueue, try_pop_from_empty_queue) {
  xenium::michael_scott_queue<int, xenium::policy::reclaimer<TypeParam>> queue;
  int elem = 0;
  ASSERT_FALSE(queue.try_pop(elem));
}

TYPED_TEST(MichaelScottQueue, pop_from_empty_queue) {
  xenium::michael_scott_queue<int, xenium::policy::reclaimer<TypeParam>> queue;
  auto elem = queue.pop();
  ASSERT_FALSE(elem.has_value());
}

TYPED_TEST(MichaelScottQueue, push_try_pop_returns_pushed_element) {
  xenium::michael_scott_queue<int, xenium::policy::reclaimer<TypeParam>> queue;
  queue.push(42);
  int elem = 0;
  ASSERT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(42, elem);
}

TYPED_TEST(MichaelScottQueue, push_pop_returns_pushed_element) {
  xenium::michael_scott_queue<int, xenium::policy::reclaimer<TypeParam>> queue;
  queue.push(42);
  auto elem = queue.pop();
  ASSERT_TRUE(elem.has_value());
  EXPECT_EQ(42, *elem);
}

TYPED_TEST(MichaelScottQueue, push_two_items_pop_them_in_FIFO_order) {
  xenium::michael_scott_queue<int, xenium::policy::reclaimer<TypeParam>> queue;
  queue.push(42);
  queue.push(43);
  auto elem1 = queue.pop();
  auto elem2 = queue.pop();
  EXPECT_TRUE(elem1.has_value());
  EXPECT_TRUE(elem2.has_value());
  EXPECT_EQ(42, *elem1);
  EXPECT_EQ(43, *elem2);
}

TYPED_TEST(MichaelScottQueue, supports_move_only_types) {
  xenium::michael_scott_queue<std::unique_ptr<int>, xenium::policy::reclaimer<TypeParam>> queue;
  queue.push(std::make_unique<int>(42));

  std::unique_ptr<int> elem;
  ASSERT_TRUE(queue.try_pop(elem));
  ASSERT_NE(nullptr, elem);
  EXPECT_EQ(42, *elem);
}

TYPED_TEST(MichaelScottQueue, supports_non_default_constructible_types) {
  xenium::michael_scott_queue<xenium::test::non_default_constructible, xenium::policy::reclaimer<TypeParam>> queue;
  queue.push(xenium::test::non_default_constructible(42));

  auto elem = queue.pop();
  ASSERT_TRUE(elem.has_value());
  EXPECT_EQ(42, elem->value);
}

TYPED_TEST(MichaelScottQueue, correctly_destroys_stored_objects) {
  int created = 0;
  int destroyed = 0;
  struct Counting {
    Counting(int& created, int& destroyed) : created(created), destroyed(destroyed) { ++created; }
    Counting(const Counting& r) noexcept : created(r.created), destroyed(r.destroyed) { ++created; }
    ~Counting() { ++destroyed; }
    int& created;
    int& destroyed;
  };
  {
    xenium::michael_scott_queue<Counting, xenium::policy::reclaimer<TypeParam>> queue;
    queue.push({created, destroyed});
    queue.push({created, destroyed});
    queue.push({created, destroyed});
    queue.push({created, destroyed});

    EXPECT_TRUE(queue.pop());
    EXPECT_TRUE(queue.pop());
    EXPECT_EQ(2, created - destroyed);
  }
  EXPECT_EQ(0, created - destroyed);
}

TYPED_TEST(MichaelScottQueue, parallel_usage) {
  using Reclaimer = TypeParam;
  xenium::michael_scott_queue<int, xenium::policy::reclaimer<Reclaimer>> queue;

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
        queue.push(i);
        int v;
        EXPECT_TRUE(queue.try_pop(v));
      }
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}
} // namespace