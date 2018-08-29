#include <citissime/reclamation/lock_free_ref_count.hpp>
#include <citissime/reclamation/hazard_pointer.hpp>
#include <citissime/reclamation/hazard_eras.hpp>
#include <citissime/reclamation/epoch_based.hpp>
#include <citissime/reclamation/new_epoch_based.hpp>
#include <citissime/reclamation/quiescent_state_based.hpp>
#include <citissime/reclamation/stamp_it.hpp>
#include <citissime/michael_harris_hash_map.hpp>

#include <gtest/gtest.h>

#include <vector>
#include <thread>

namespace {

template <typename Reclaimer>
struct HashMap : ::testing::Test
{
  using hash_map = citissime::michael_harris_hash_map<int, int, Reclaimer, 10>;
  hash_map map;
};

using Reclaimers = ::testing::Types<
    citissime::reclamation::lock_free_ref_count<>,
    citissime::reclamation::hazard_pointer<citissime::reclamation::static_hazard_pointer_policy<3>>,
    citissime::reclamation::hazard_eras<citissime::reclamation::static_hazard_eras_policy<3>>,
    citissime::reclamation::epoch_based<10>,
    citissime::reclamation::new_epoch_based<10>,
    citissime::reclamation::quiescent_state_based,
    citissime::reclamation::stamp_it
  >;
TYPED_TEST_CASE(HashMap, Reclaimers);

TYPED_TEST(HashMap, insert_returns_true_and_sets_entry_when_successful)
{
  typename TestFixture::hash_map::guard_ptr entry;
  EXPECT_TRUE(this->map.insert(42, 43, entry));
  ASSERT_NE(nullptr, entry.get());
  EXPECT_EQ(42, entry->key);
  EXPECT_EQ(43, entry->value);
}

TYPED_TEST(HashMap, insert_same_element_twice_second_time_fails_but_returns_first_entry)
{
  EXPECT_TRUE(this->map.insert(42, 43));

  typename TestFixture::hash_map::guard_ptr entry;
  EXPECT_FALSE(this->map.insert(42, 44, entry));
  ASSERT_NE(nullptr, entry.get());
  EXPECT_EQ(42, entry->key);
  EXPECT_EQ(43, entry->value);
}

TYPED_TEST(HashMap, search_for_non_existing_element_returns_null)
{
  EXPECT_EQ(nullptr, this->map.search(43));
}

TYPED_TEST(HashMap, search_for_existing_element_returns_valid_ptr)
{
  this->map.insert(42, 43);
  auto result = this->map.search(42);
  ASSERT_NE(nullptr, result.get());
  EXPECT_EQ(42, result->key);
  EXPECT_EQ(43, result->value);
}

TYPED_TEST(HashMap, remove_existing_element_succeeds)
{
  this->map.insert(42, 43);
  EXPECT_TRUE(this->map.remove(42));
}

TYPED_TEST(HashMap, remove_nonexisting_element_fails)
{
  EXPECT_FALSE(this->map.remove(42));
}

TYPED_TEST(HashMap, remove_existing_element_twice_fails_the_seond_time)
{
  this->map.insert(42, 43);
  EXPECT_TRUE(this->map.remove(42));
  EXPECT_FALSE(this->map.remove(42));
}

namespace
{
#ifdef DEBUG
  const int MaxIterations = 1000;
#else
  const int MaxIterations = 10000;
#endif
}

TYPED_TEST(HashMap, parallel_usage)
{
  using Reclaimer = TypeParam;

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
  {
    threads.push_back(std::thread([i, this]
    {
      for (int j = 0; j < MaxIterations; ++j)
      {
        typename Reclaimer::region_guard critical_region{};
        EXPECT_EQ(nullptr, this->map.search(i));
        EXPECT_TRUE(this->map.insert(i, i));
        auto node = this->map.search(i);
        EXPECT_NE(nullptr, node.get());
        EXPECT_EQ(i, node->key);
        EXPECT_EQ(i, node->value);
        EXPECT_TRUE(this->map.remove(i));
      }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

TYPED_TEST(HashMap, parallel_usage_with_same_values)
{
  using Reclaimer = TypeParam;

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
  {
    threads.push_back(std::thread([this]
    {
      for (int j = 0; j < MaxIterations / 10; ++j)
        for (int i = 0; i < 10; ++i)
        {
          typename Reclaimer::region_guard critical_region{};
          this->map.search(i);
          this->map.insert(i, i);
          this->map.search(i);
          this->map.remove(i);
        }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

}