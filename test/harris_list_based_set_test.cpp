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

TYPED_TEST(List, emplace_same_element_twice_second_time_fails)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  EXPECT_TRUE(list.emplace(42));
  EXPECT_FALSE(list.emplace(42));
}

TYPED_TEST(List, emplace_or_get_inserts_new_element_and_returns_iterator_to_it)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  auto result = list.emplace_or_get(42);
  EXPECT_TRUE(result.second);
  EXPECT_EQ(list.begin(), result.first);
  EXPECT_EQ(42, *result.first);
}

TYPED_TEST(List, emplace_or_get_does_not_insert_anything_and_returns_iterator_to_existing_element)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.emplace(42);
  auto result = list.emplace_or_get(42);
  EXPECT_FALSE(result.second);
  EXPECT_EQ(list.begin(), result.first);
  EXPECT_EQ(42, *result.first);
}

TYPED_TEST(List, contains_returns_false_for_non_existing_element)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.emplace(42);
  EXPECT_FALSE(list.contains(43));
}

TYPED_TEST(List, constains_returns_true_for_existing_element)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.emplace(42);
  EXPECT_TRUE(list.contains(42));
}

TYPED_TEST(List, find_returns_end_iterator_for_non_existing_element)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.emplace(42);
  EXPECT_EQ(list.end(), list.find(43));
}

TYPED_TEST(List, find_returns_matching_iterator_for_existing_element)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.emplace(42);
  auto it = list.find(42);
  EXPECT_EQ(list.begin(), it);
  EXPECT_EQ(42, *it);
  EXPECT_EQ(list.end(), ++it);
}

TYPED_TEST(List, erase_existing_element_succeeds)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.emplace(42);
  EXPECT_TRUE(list.erase(42));
}

TYPED_TEST(List, erase_nonexisting_element_fails)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  EXPECT_FALSE(list.erase(42));
}

TYPED_TEST(List, erase_existing_element_twice_fails_the_seond_time)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.emplace(42);
  EXPECT_TRUE(list.erase(42));
  EXPECT_FALSE(list.erase(42));
}

TYPED_TEST(List, erase_via_iterator_removes_entry_and_returns_iterator_to_successor)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.emplace(41);
  list.emplace(42);
  list.emplace(43);

  auto it = list.find(42);

  it = list.erase(std::move(it));
  ASSERT_NE(list.end(), it);
  EXPECT_EQ(43, *it);
  it = list.end(); // reset the iterator to clear all internal guard_ptrs

  EXPECT_FALSE(list.contains(42));
}

TYPED_TEST(List, iterate_list)
{
  citissime::harris_list_based_set<int, TypeParam> list;
  list.emplace(41);
  list.emplace(42);
  list.emplace(43);

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
        EXPECT_FALSE(list.contains(i));
        EXPECT_TRUE(list.emplace(i));
        EXPECT_TRUE(list.contains(i));
        EXPECT_TRUE(list.erase(i));

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
          list.contains(i);
          list.emplace(i);
          list.contains(i);
          list.erase(i);

          for(auto& v : list)
            EXPECT_TRUE(v >= 0 && v < 10);
        }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

}