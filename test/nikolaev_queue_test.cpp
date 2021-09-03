#include <xenium/nikolaev_queue.hpp>
#include <xenium/reclamation/generic_epoch_based.hpp>
#include <xenium/reclamation/hazard_eras.hpp>
#include <xenium/reclamation/hazard_pointer.hpp>
#include <xenium/reclamation/lock_free_ref_count.hpp>
#include <xenium/reclamation/quiescent_state_based.hpp>
#include <xenium/reclamation/stamp_it.hpp>

#include <gtest/gtest.h>

#include <random>
#include <thread>
#include <vector>

namespace {

template <typename Reclaimer>
struct NikolaevQueue : testing::Test {};

struct non_default_constructible {
  explicit non_default_constructible(int x) : x(x) {}
  int x;
};

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
TYPED_TEST_SUITE(NikolaevQueue, Reclaimers);

TYPED_TEST(NikolaevQueue, push_try_pop_returns_pushed_element) {
  xenium::nikolaev_queue<int, xenium::policy::reclaimer<TypeParam>> queue;
  queue.push(42);
  int elem = 0;
  ASSERT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(42, elem);
}

TYPED_TEST(NikolaevQueue, push_two_items_pop_them_in_FIFO_order) {
  xenium::nikolaev_queue<int, xenium::policy::reclaimer<TypeParam>> queue;
  queue.push(42);
  queue.push(43);
  int elem1 = 0;
  int elem2 = 0;
  EXPECT_TRUE(queue.try_pop(elem1));
  ASSERT_TRUE(queue.try_pop(elem2));
  EXPECT_EQ(42, elem1);
  EXPECT_EQ(43, elem2);
}

TYPED_TEST(NikolaevQueue, try_pop_returns_false_when_queue_is_empty) {
  xenium::nikolaev_queue<int, xenium::policy::reclaimer<TypeParam>> queue;
  int elem;
  EXPECT_FALSE(queue.try_pop(elem));
}

TYPED_TEST(NikolaevQueue, supports_move_only_types) {
  xenium::nikolaev_queue<std::pair<int, std::unique_ptr<int>>, xenium::policy::reclaimer<TypeParam>> queue;
  queue.push({41, std::make_unique<int>(42)});

  std::pair<int, std::unique_ptr<int>> elem;
  ASSERT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(41, elem.first);
  ASSERT_NE(nullptr, elem.second);
  EXPECT_EQ(42, *elem.second);
}

TYPED_TEST(NikolaevQueue, supports_non_default_constructible_types) {
  xenium::nikolaev_queue<non_default_constructible, xenium::policy::reclaimer<TypeParam>> queue;
  queue.push(non_default_constructible(42));

  non_default_constructible elem(0);
  ASSERT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(42, elem.x);
}

TYPED_TEST(NikolaevQueue, deletes_remaining_entries) {
  unsigned delete_count = 0;
  struct dummy {
    unsigned& delete_count;
    explicit dummy(unsigned& delete_count) : delete_count(delete_count) {}
    ~dummy() { ++delete_count; }
  };
  {
    xenium::nikolaev_queue<std::unique_ptr<dummy>, xenium::policy::reclaimer<TypeParam>> queue;
    queue.push(std::make_unique<dummy>(delete_count));
  }
  EXPECT_EQ(1u, delete_count);
}

TYPED_TEST(NikolaevQueue, push_pop_in_fifo_order_with_remapped_indexes) {
  constexpr int capacity = 11;
  xenium::nikolaev_queue<int, xenium::policy::entries_per_node<8>, xenium::policy::reclaimer<TypeParam>> queue;
  for (int i = 0; i < capacity; ++i) {
    queue.push(i);
  }

  for (int i = 0; i < capacity; ++i) {
    int value;
    ASSERT_TRUE(queue.try_pop(value)) << "iteration " << i;
    EXPECT_EQ(i, value);
  }
}

#ifdef DEBUG
const int MaxIterations = 10000;
#else
const int MaxIterations = 50000;
#endif

TYPED_TEST(NikolaevQueue, parallel_usage) {
  using Reclaimer = TypeParam;
  xenium::nikolaev_queue<int, xenium::policy::reclaimer<TypeParam>, xenium::policy::entries_per_node<8>> queue;

  constexpr int num_threads = 4;
  constexpr int thread_mask = num_threads - 1;

  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads; ++i) {
    threads.push_back(std::thread([i, &queue, num_threads, thread_mask] {
      // oh my... MSVC complains if these variables are NOT captured; clang complains if they ARE captured.
      (void)num_threads;
      (void)thread_mask;

      std::vector<int> last_seen(num_threads);
      int counter = 0;
      for (int j = 0; j < MaxIterations; ++j) {
        [[maybe_unused]] typename Reclaimer::region_guard guard{};
        queue.push((++counter << 8) | i);
        int elem = 0;
        ASSERT_TRUE(queue.try_pop(elem));
        int thread = elem & thread_mask;
        elem >>= 8;
        EXPECT_GT(elem, last_seen[thread]);
        last_seen[thread] = elem;
      }
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TYPED_TEST(NikolaevQueue, parallel_usage_mostly_full) {
  using Reclaimer = TypeParam;
  xenium::nikolaev_queue<int, xenium::policy::reclaimer<TypeParam>, xenium::policy::entries_per_node<8>> queue;
  for (int i = 0; i < 8; ++i) {
    queue.push(1);
  }

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.push_back(std::thread([i, &queue] {
      std::mt19937_64 rand;
      rand.seed(i);

      for (int j = 0; j < MaxIterations; ++j) {
        [[maybe_unused]] typename Reclaimer::region_guard guard{};
        if (rand() % 128 < 64) {
          queue.push(i);
        } else {
          int elem;
          if (queue.try_pop(elem)) {
            EXPECT_TRUE(elem >= 0 && elem <= 4);
          }
        }
      }
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TYPED_TEST(NikolaevQueue, parallel_usage_mostly_empty) {
  using Reclaimer = TypeParam;
  xenium::nikolaev_queue<int, xenium::policy::reclaimer<TypeParam>, xenium::policy::entries_per_node<8>> queue;

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.push_back(std::thread([i, &queue] {
      std::mt19937_64 rand;
      rand.seed(i);

      for (int j = 0; j < MaxIterations; ++j) {
        [[maybe_unused]] typename Reclaimer::region_guard guard{};
        if (rand() % 128 < 16) {
          queue.push(i);
        } else {
          int elem;
          if (queue.try_pop(elem)) {
            EXPECT_TRUE(elem >= 0 && elem <= 4);
          }
        }
      }
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}
} // namespace