#include <citissime/reclamation/lock_free_ref_count.hpp>
#include <citissime/reclamation/hazard_pointer.hpp>
#include <citissime/reclamation/hazard_eras.hpp>
#include <citissime/reclamation/epoch_based.hpp>
#include <citissime/reclamation/new_epoch_based.hpp>
#include <citissime/reclamation/quiescent_state_based.hpp>
#include <citissime/reclamation/stamp_it.hpp>
#include <citissime/harris_list_based_set.hpp>

#include <gtest/gtest.h>

#include <vector>
#include <thread>

namespace {

template <typename Reclaimer>
struct List : testing::Test {};

using Reclaimers = ::testing::Types<
    citissime::reclamation::lock_free_ref_count<>,
    citissime::reclamation::hazard_pointer<citissime::reclamation::static_hazard_pointer_policy<3>>,
    citissime::reclamation::hazard_eras<citissime::reclamation::static_hazard_eras_policy<3>>,
    citissime::reclamation::epoch_based<10>,
    citissime::reclamation::new_epoch_based<10>,
    citissime::reclamation::quiescent_state_based,
    citissime::reclamation::stamp_it
  >;
TYPED_TEST_CASE(List, Reclaimers);

TYPED_TEST(List, insert_same_element_twice_second_time_fails)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  EXPECT_TRUE(list.insert(42));
  EXPECT_FALSE(list.insert(42));
}

TYPED_TEST(List, search_for_non_existing_element_returns_false)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.insert(42);
  EXPECT_FALSE(list.search(43));
}

TYPED_TEST(List, search_for_existing_element_returns_true)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.insert(42);
  EXPECT_TRUE(list.search(42));
}

TYPED_TEST(List, remove_existing_element_succeeds)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.insert(42);
  EXPECT_TRUE(list.remove(42));
}

TYPED_TEST(List, remove_nonexisting_element_fails)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  EXPECT_FALSE(list.remove(42));
}

TYPED_TEST(List, remove_existing_element_twice_fails_the_seond_time)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.insert(42);
  EXPECT_TRUE(list.remove(42));
  EXPECT_FALSE(list.remove(42));
}

TYPED_TEST(List, iterate_list)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.insert(41);
  list.insert(42);
  list.insert(43);

  auto it = list.begin();
  EXPECT_EQ(41, *it);
  ++it;
  EXPECT_EQ(42, *it);
  ++it;
  EXPECT_EQ(43, *it);
  ++it;
  EXPECT_EQ(list.end(), it);
}

namespace
{
#ifdef DEBUG
  const int MaxIterations = 1000;
#else
  const int MaxIterations = 10000;
#endif
}

TYPED_TEST(List, parallel_usage)
{
  using Reclaimer = TypeParam;
  citissime::harris_list_based_set<int, TypeParam> list;

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
  {
    threads.push_back(std::thread([i, &list]
    {
      for (int j = 0; j < MaxIterations; ++j)
      {
        typename Reclaimer::region_guard critical_region{};
        EXPECT_FALSE(list.search(i));
        EXPECT_TRUE(list.insert(i));
        EXPECT_TRUE(list.search(i));
        EXPECT_TRUE(list.remove(i));

        for(auto& v : list)
          EXPECT_TRUE(v >= 0 && v < 8);
      }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

TYPED_TEST(List, parallel_usage_with_same_values)
{
  using Reclaimer = TypeParam;
  citissime::harris_list_based_set<int, TypeParam> list;

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
  {
    threads.push_back(std::thread([&list]
    {
      for (int j = 0; j < MaxIterations / 10; ++j)
        for (int i = 0; i < 10; ++i)
        {
          typename Reclaimer::region_guard critical_region{};
          list.search(i);
          list.insert(i);
          list.search(i);
          list.remove(i);

          for(auto& v : list)
            EXPECT_TRUE(v >= 0 && v < 10);
        }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

}