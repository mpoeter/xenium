#include <xenium/reclamation/hazard_pointer.hpp>
#include <xenium/reclamation/hazard_eras.hpp>
#include <xenium/reclamation/epoch_based.hpp>
#include <xenium/reclamation/new_epoch_based.hpp>
#include <xenium/reclamation/quiescent_state_based.hpp>
#include <xenium/reclamation/debra.hpp>
#include <xenium/reclamation/generic_epoch_based.hpp>
#include <xenium/reclamation/stamp_it.hpp>
#include <xenium/vyukov_hash_map.hpp>

#include <gtest/gtest.h>

#include <vector>
#include <thread>

namespace {

template <typename Reclaimer>
struct VyukovHashMap : ::testing::Test
{
  using hash_map = xenium::vyukov_hash_map<int, int,
    xenium::policy::reclaimer<Reclaimer>>;
  hash_map map{8};
};

using Reclaimers = ::testing::Types<
    xenium::reclamation::hazard_pointer<xenium::reclamation::static_hazard_pointer_policy<3>>,
    xenium::reclamation::hazard_eras<xenium::reclamation::static_hazard_eras_policy<3>>,
    xenium::reclamation::epoch_based<10>,
    xenium::reclamation::new_epoch_based<10>,
    xenium::reclamation::quiescent_state_based,
    xenium::reclamation::debra<20>,
    xenium::reclamation::stamp_it,
    xenium::reclamation::epoch_based2<>,
    xenium::reclamation::new_epoch_based2<>,
    xenium::reclamation::debra2<>
  >;
TYPED_TEST_CASE(VyukovHashMap, Reclaimers);

TYPED_TEST(VyukovHashMap, emplace_returns_true_for_successful_insert)
{
  EXPECT_TRUE(this->map.emplace(42, 42));
}

TYPED_TEST(VyukovHashMap, emplace_returns_false_for_failed_insert)
{
  this->map.emplace(42, 42);
  EXPECT_FALSE(this->map.emplace(42, 43));
}

TYPED_TEST(VyukovHashMap, try_get_value_returns_false_key_is_not_found)
{
  int v;
  EXPECT_FALSE(this->map.try_get_value(42, v));
}

TYPED_TEST(VyukovHashMap, try_get_value_returns_true_and_sets_result_if_matching_entry_exists)
{
  this->map.emplace(42, 43);
  int v;
  EXPECT_TRUE(this->map.try_get_value(42, v));
  EXPECT_EQ(v, 43);
}

TYPED_TEST(VyukovHashMap, erase_nonexisting_element_returns_false)
{
  EXPECT_FALSE(this->map.erase(42));
}

TYPED_TEST(VyukovHashMap, erase_existing_element_returns_true_and_removes_element)
{
  this->map.emplace(42, 43);
  EXPECT_TRUE(this->map.erase(42));
  EXPECT_FALSE(this->map.erase(42));
}

TYPED_TEST(VyukovHashMap, extract_existing_element_returns_true_and_removes_element_and_returns_old_value)
{
  this->map.emplace(42, 43);
  int v;
  EXPECT_TRUE(this->map.extract(42, v));
  EXPECT_EQ(43, v);
  EXPECT_FALSE(this->map.erase(42));
}

TYPED_TEST(VyukovHashMap, map_grows_if_needed)
{
  for (int i = 0; i < 10000; ++i)
    EXPECT_TRUE(this->map.emplace(i, i));
}

TYPED_TEST(VyukovHashMap, with_managed_pointer_value)
{
  struct node : TypeParam::template enable_concurrent_ptr<node> {
    node(int v): v(v) {}
    int v;
  };

  using hash_map = xenium::vyukov_hash_map<int, node*,
    xenium::policy::reclaimer<TypeParam>, xenium::policy::value_reclaimer<TypeParam>>;
  hash_map map;

  EXPECT_TRUE(map.emplace(42, new node(43)));
  typename hash_map::accessor accessor;
  EXPECT_TRUE(map.try_get_value(42, accessor));
  EXPECT_EQ(accessor->v, 43);
  EXPECT_TRUE(map.erase(42));

  EXPECT_TRUE(map.emplace(42, new node(44)));
  EXPECT_TRUE(map.extract(42, accessor));
  EXPECT_EQ(accessor->v, 44);
  accessor.reclaim();
}

TYPED_TEST(VyukovHashMap, with_string_value)
{
  using hash_map = xenium::vyukov_hash_map<int, std::string, xenium::policy::reclaimer<TypeParam>>;
  hash_map map;

  EXPECT_TRUE(map.emplace(42, "foo"));
  typename hash_map::accessor accessor;
  EXPECT_TRUE(map.try_get_value(42, accessor));
  EXPECT_EQ(*accessor, "foo");
  EXPECT_TRUE(map.erase(42));

  EXPECT_TRUE(map.emplace(42, "bar"));
  auto it = map.begin();
  EXPECT_EQ((*it).first, 42);
  EXPECT_EQ((*it).second, "bar");

  it.reset();

  for (auto v : map) {
    EXPECT_EQ(v.first, 42);
    EXPECT_EQ(v.second, "bar");
  }
  
  EXPECT_TRUE(map.extract(42, accessor));
  EXPECT_EQ(*accessor, "bar");
}

TYPED_TEST(VyukovHashMap, with_string_key)
{
  using hash_map = xenium::vyukov_hash_map<std::string, int, xenium::policy::reclaimer<TypeParam>>;
  hash_map map;

  EXPECT_TRUE(map.emplace("foo", 42));
  typename hash_map::accessor accessor;
  EXPECT_TRUE(map.try_get_value("foo", accessor));
  EXPECT_EQ(*accessor, 42);
  EXPECT_TRUE(map.erase("foo"));

  EXPECT_TRUE(map.emplace("foo", 43));
  auto it = map.begin();
  EXPECT_EQ((*it).first, "foo");
  EXPECT_EQ((*it).second, 43);
  EXPECT_EQ(it->first, "foo");
  EXPECT_EQ(it->second, 43);
  it.reset();

  for (auto& v : map) {
    EXPECT_EQ(v.first, "foo");
    EXPECT_EQ(v.second, 43);
  }

  EXPECT_TRUE(map.extract("foo", accessor));
  EXPECT_EQ(*accessor, 43);
}

TYPED_TEST(VyukovHashMap, correctly_handles_hash_collisions_of_nontrivial_keys)
{
  struct dummy_hash {
    dummy_hash() = default;
    std::size_t operator()(const std::string&) { return 1; }
  };
  using hash_map = xenium::vyukov_hash_map<std::string, int,
    xenium::policy::reclaimer<TypeParam>, xenium::policy::hash<dummy_hash>>;
  hash_map map;

  EXPECT_TRUE(map.emplace("foo", 42));
  EXPECT_TRUE(map.emplace("bar", 43));
  typename hash_map::accessor accessor;
  EXPECT_TRUE(map.try_get_value("foo", accessor));
  EXPECT_EQ(*accessor, 42);
  EXPECT_TRUE(map.try_get_value("bar", accessor));
  EXPECT_EQ(*accessor, 43);
  
  EXPECT_TRUE(map.extract("foo", accessor));
  EXPECT_EQ(*accessor, 42);
}

TYPED_TEST(VyukovHashMap, begin_returns_end_iterator_for_empty_map)
{
  auto it = this->map.begin();
  ASSERT_EQ(this->map.end(), it);
}

TYPED_TEST(VyukovHashMap, begin_returns_iterator_to_first_entry)
{
  this->map.emplace(42, 43);
  auto it = this->map.begin();
  ASSERT_NE(this->map.end(), it);
  EXPECT_EQ(42, (*it).first);
  EXPECT_EQ(43, (*it).second);
  ++it;
  ASSERT_EQ(this->map.end(), it);
}

TYPED_TEST(VyukovHashMap, drain_densely_populated_map_using_erase)
{
  for (int i = 0; i < 200; ++i)
    this->map.emplace(i, i);

  auto it = this->map.begin();
  while (it != this->map.end())
    this->map.erase(it);

  EXPECT_EQ(this->map.end(), this->map.begin());
}

TYPED_TEST(VyukovHashMap, drain_sparsely_populated_map_using_erase)
{
  for (int i = 0; i < 4; ++i)
    this->map.emplace(i * 7, i);

  auto it = this->map.begin();
  while (it != this->map.end())
    this->map.erase(it);

  EXPECT_EQ(this->map.end(), this->map.begin());
}

TYPED_TEST(VyukovHashMap, iterator_covers_all_entries_in_densely_populated_map)
{
  std::map<int, bool> values;
  for (int i = 0; i < 200; ++i) {
    values[i] = false;
    this->map.emplace(i, i);
  }
  for (auto v : this->map)
    values[v.first] = true;
  for (auto& v : values)
    EXPECT_TRUE(v.second) << v.first << " was not visited";
}

TYPED_TEST(VyukovHashMap, iterator_covers_all_entries_in_sparsely_populated_map)
{
  std::map<int, bool> values;
  for (int i = 0; i < 4; ++i) {
    values[i * 7] = false;
    this->map.emplace(i * 7, i);
  }
  for (auto v : this->map)
    values[v.first] = true;

  for (auto& v : values)
    EXPECT_TRUE(v.second) << v.first << " was not visited";
}

#ifdef DEBUG
  const int MaxIterations = 2000;
#else
  const int MaxIterations = 40000;
#endif

TYPED_TEST(VyukovHashMap, parallel_usage)
{
  using Reclaimer = TypeParam;

  using hash_map = xenium::vyukov_hash_map<int, int,
    xenium::policy::reclaimer<Reclaimer>>;
  hash_map map(8);

  static constexpr int keys_per_thread = 8;

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
  {
    threads.push_back(std::thread([i, &map]
    {
      for (int k = i * keys_per_thread; k < (i + 1) * keys_per_thread; ++k) {
        for (int j = 0; j < MaxIterations / keys_per_thread; ++j) {
          typename Reclaimer::region_guard critical_region{};
          EXPECT_TRUE(map.emplace(k, k));
          for (int x = 0; x < 10; ++x) {
            int v = 0;
            EXPECT_TRUE(map.try_get_value(k, v));
            EXPECT_EQ(v, k);
          }
          if (j % 8 == 0) {
            for (auto v : map)
              EXPECT_EQ(v.first, v.second);
          }
          EXPECT_TRUE(map.erase(k));
        }
      }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

TYPED_TEST(VyukovHashMap, parallel_usage_with_nontrivial_types)
{
  using Reclaimer = TypeParam;

  using hash_map = xenium::vyukov_hash_map<std::string, std::string,
    xenium::policy::reclaimer<Reclaimer>>;
  hash_map map(8);

  static constexpr int keys_per_thread = 8;

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
  {
    threads.push_back(std::thread([i, &map]
    {
      for (int k = i * keys_per_thread; k < (i + 1) * keys_per_thread; ++k) {
        for (int j = 0; j < MaxIterations / keys_per_thread; ++j) {
          std::string v = std::to_string(k);
          typename Reclaimer::region_guard critical_region{};
          EXPECT_TRUE(map.emplace(v, v));
          for (int x = 0; x < 10; ++x) {
            typename hash_map::accessor a;
            EXPECT_TRUE(map.try_get_value(v, a));
            EXPECT_EQ(*a, v);
          }
          if (j % 8 == 0) {
            for (auto& v : map)
              EXPECT_EQ(v.first, v.second);
          }
          EXPECT_TRUE(map.erase(v));
        }
      }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

TYPED_TEST(VyukovHashMap, parallel_usage_with_same_values)
{
  using Reclaimer = TypeParam;

  using hash_map = xenium::vyukov_hash_map<int, int,
    xenium::policy::reclaimer<Reclaimer>>;
  hash_map map(8);

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
  {
    threads.push_back(std::thread([&map]
    {
      for (int j = 0; j < MaxIterations / 10; ++j)
        for (int i = 0; i < 10; ++i)
        {
          int k = i;
          typename Reclaimer::region_guard critical_region{};
          map.emplace(k, i);
          int v = 0;
          if (map.try_get_value(k, v))
            EXPECT_EQ(v, k);

          if (j % 4 == 0) {
            for (auto v : map)
              ;
          }
          map.erase(k);
        }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

}