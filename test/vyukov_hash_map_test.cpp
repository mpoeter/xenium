#include <xenium/meta.hpp>
#include <xenium/reclamation/generic_epoch_based.hpp>
#include <xenium/reclamation/hazard_eras.hpp>
#include <xenium/reclamation/hazard_pointer.hpp>
#include <xenium/reclamation/quiescent_state_based.hpp>
#include <xenium/reclamation/stamp_it.hpp>
#include <xenium/vyukov_hash_map.hpp>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable : 4458) // declaration hides member
#endif

namespace {
// Simple solution to simulate exception beeing thrown in "compare_key". Such
// exceptions can be caused by the construction of guard_ptr instances (e.g.,
// when using hazard_pointer reclaimer).
struct throwing_key {
  explicit throwing_key(int v) noexcept : v(v) {}
  int v;
  bool operator==(const throwing_key&) const { throw std::runtime_error("test exception"); }
};
} // namespace

namespace xenium {
template <>
struct hash<throwing_key> {
  hash_t operator()(const throwing_key& v) const { return v.v; }
};
} // namespace xenium

namespace {

struct managed_ptr_tag {};

struct managed_ptr_value {
  int v;

  // for comparison with accessor values
  template <class T>
  bool operator==(const T& other) const noexcept {
    return this->v == other.v;
  }

  // for comparison with iterator values
  template <class T>
  bool operator==(T* const other) const noexcept {
    return this->v == other->v;
  }

  inline friend std::ostream& operator<<(std::ostream& os, const managed_ptr_value& p) { return os << p.v; }
};

template <class Key, class Value, class Reclaimer>
struct test_params {
  using key_type = Key;
  using value_type = Value;
  using reclaimer = Reclaimer;
};

template <class>
struct test_params_map;

template <class Key, class Value, class Reclaimer>
struct test_params_map<std::pair<std::pair<Key, Value>, Reclaimer>> {
  using type = test_params<Key, Value, Reclaimer>;
};

using KeyTypes = xenium::meta::type_list<int, std::string>;
using ValueTypes = xenium::meta::type_list<int, std::string, managed_ptr_tag>;
using Reclaimers = xenium::meta::type_list<
  xenium::reclamation::hazard_pointer<>::with<
    xenium::policy::allocation_strategy<xenium::reclamation::hp_allocation::static_strategy<5>>>,
  xenium::reclamation::hazard_eras<>::with<
    xenium::policy::allocation_strategy<xenium::reclamation::he_allocation::static_strategy<5>>>,
  xenium::reclamation::quiescent_state_based,
  xenium::reclamation::stamp_it,
  xenium::reclamation::epoch_based<>::with<xenium::policy::scan_frequency<10>>,
  xenium::reclamation::new_epoch_based<>::with<xenium::policy::scan_frequency<10>>,
  xenium::reclamation::debra<>::with<xenium::policy::scan_frequency<10>>>;

template <class>
struct as_testing_types;

template <class... Ts>
struct as_testing_types<xenium::meta::type_list<Ts...>> {
  using type = ::testing::Types<Ts...>;
};

template <typename Params>
struct VyukovHashMap : ::testing::Test {
  using KeyType = typename Params::key_type;
  using ValueType = typename Params::value_type;
  using Reclaimer = typename Params::reclaimer;

  struct node : Reclaimer::template enable_concurrent_ptr<node> {
    explicit node(int v) : v(v) {}
    int v;

    inline friend std::ostream& operator<<(std::ostream& os, const node& n) { return os << n.v; }
  };

  using MapValueType =
    std::conditional_t<std::is_same_v<ValueType, managed_ptr_tag>, xenium::managed_ptr<node, Reclaimer>, ValueType>;

  using hash_map = xenium::vyukov_hash_map<KeyType, MapValueType, xenium::policy::reclaimer<Reclaimer>>;
  hash_map map{8};

  auto make_key(int v) {
    if constexpr (std::is_same_v<KeyType, int>) {
      return v;
    } else if constexpr (std::is_same_v<KeyType, std::string>) {
      return std::to_string(v);
    } else if constexpr (std::is_same_v<KeyType, managed_ptr_tag>) {
      return xenium::managed_ptr<node, Reclaimer>(new int(v));
    }
  }

  auto make_value(int v) {
    if constexpr (std::is_same_v<ValueType, int>) {
      return v;
    } else if constexpr (std::is_same_v<ValueType, std::string>) {
      return std::to_string(v);
    } else if constexpr (std::is_same_v<ValueType, managed_ptr_tag>) {
      return new node(v);
    }
  }

  auto make_comparison_value(int v) {
    if constexpr (std::is_same_v<ValueType, managed_ptr_tag>) {
      return managed_ptr_value{v};
    } else {
      return make_value(v);
    }
  }
};
using TestParamsList = as_testing_types<
  xenium::meta::map<xenium::meta::cross_product<xenium::meta::cross_product<KeyTypes, ValueTypes>, Reclaimers>,
                    test_params_map>>::type;
TYPED_TEST_SUITE(VyukovHashMap, TestParamsList);

TYPED_TEST(VyukovHashMap, emplace_returns_true_for_successful_insert) {
  EXPECT_TRUE(this->map.emplace(this->make_key(42), this->make_value(42)));
}

TYPED_TEST(VyukovHashMap, emplace_returns_false_for_failed_insert) {
  this->map.emplace(this->make_key(42), this->make_value(42));
  EXPECT_FALSE(this->map.emplace(this->make_key(42), this->make_value(43)));
}

TYPED_TEST(VyukovHashMap, get_or_emplace_returns_accessor_to_newly_inserted_element) {
  auto result = this->map.get_or_emplace(this->make_key(42), this->make_value(43));
  EXPECT_TRUE(result.second);
  EXPECT_EQ(this->make_comparison_value(43), *result.first);
}

TYPED_TEST(VyukovHashMap, get_or_emplace_returns_accessor_to_existing_element) {
  this->map.emplace(this->make_key(42), this->make_value(41));
  auto result = this->map.get_or_emplace(this->make_key(42), this->make_value(43));
  EXPECT_FALSE(result.second);
  EXPECT_EQ(this->make_comparison_value(41), *result.first);
}

TYPED_TEST(VyukovHashMap, get_or_emplace_lazy_calls_factory_and_returns_accessor_to_newly_inserted_element) {
  bool called_factory = false;
  auto result = this->map.get_or_emplace_lazy(this->make_key(42), [&]() {
    called_factory = true;
    return this->make_value(43);
  });
  EXPECT_TRUE(result.second);
  EXPECT_EQ(this->make_comparison_value(43), *result.first);
  EXPECT_TRUE(called_factory);
}

TYPED_TEST(VyukovHashMap, get_or_emplace_lazy_does_not_call_factory_and_returns_accessor_to_existing_element) {
  bool called_factory = false;
  this->map.emplace(this->make_key(42), this->make_value(41));
  auto result = this->map.get_or_emplace_lazy(this->make_key(42), [&]() {
    called_factory = true;
    return this->make_value(43);
  });
  EXPECT_FALSE(result.second);
  EXPECT_EQ(this->make_comparison_value(41), *result.first);
  EXPECT_FALSE(called_factory);
}

TYPED_TEST(VyukovHashMap, try_get_value_returns_false_key_is_not_found) {
  typename TestFixture::hash_map::accessor acc;
  EXPECT_FALSE(this->map.try_get_value(this->make_key(42), acc));
}

TYPED_TEST(VyukovHashMap, try_get_value_returns_true_and_sets_result_if_matching_entry_exists) {
  this->map.emplace(this->make_key(42), this->make_value(43));
  typename TestFixture::hash_map::accessor acc;
  EXPECT_TRUE(this->map.try_get_value(this->make_key(42), acc));
  EXPECT_EQ(this->make_comparison_value(43), *acc);
}

TYPED_TEST(VyukovHashMap, find_returns_iterator_to_existing_element) {
  // We use a for loop to ensure that we cover cases where entries are
  // stored in normal buckets as well as extension buckets.
  for (int i = 0; i < 200; ++i) {
    this->map.emplace(this->make_key(i), this->make_value(i));
    auto it = this->map.find(this->make_key(i));
    ASSERT_NE(this->map.end(), it);
    EXPECT_EQ(this->make_key(i), (*it).first);
    EXPECT_EQ(this->make_comparison_value(i), (*it).second);
  }
}

TYPED_TEST(VyukovHashMap, find_returns_end_iterator_for_non_existing_element) {
  for (int i = 0; i < 200; ++i) {
    if (i != 42) {
      this->map.emplace(this->make_key(i), this->make_value(i));
    }
  }
  EXPECT_EQ(this->map.end(), this->map.find(this->make_key(42)));
}

TYPED_TEST(VyukovHashMap, erase_nonexisting_element_returns_false) {
  EXPECT_FALSE(this->map.erase(this->make_key(42)));
}

TYPED_TEST(VyukovHashMap, erase_existing_element_returns_true_and_removes_element) {
  this->map.emplace(this->make_key(42), this->make_value(43));
  EXPECT_TRUE(this->map.erase(this->make_key(42)));
  EXPECT_FALSE(this->map.erase(this->make_key(42)));
}

TYPED_TEST(VyukovHashMap, extract_existing_element_returns_true_and_removes_element_and_returns_old_value) {
  this->map.emplace(this->make_key(42), this->make_value(43));
  typename TestFixture::hash_map::accessor acc;
  EXPECT_TRUE(this->map.extract(this->make_key(42), acc));
  EXPECT_EQ(this->make_comparison_value(43), *acc);
  EXPECT_FALSE(this->map.erase(this->make_key(42)));
}

TYPED_TEST(VyukovHashMap, extract_non_existing_element_returns_false) {
  typename TestFixture::hash_map::accessor acc;
  EXPECT_FALSE(this->map.extract(this->make_key(42), acc));
}

TYPED_TEST(VyukovHashMap, map_grows_if_needed) {
  for (int i = 0; i < 10000; ++i) {
    EXPECT_TRUE(this->map.emplace(this->make_key(i), this->make_value(i)));
  }
}

TYPED_TEST(VyukovHashMap, emplace_unlocks_bucket_in_case_of_exception) {
  this->map.emplace(this->make_key(42), this->make_value(42));
  EXPECT_THROW(
    this->map.get_or_emplace_lazy(
      this->make_key(43), []() -> decltype(this->make_value(42)) { throw std::runtime_error("test exception"); }),
    std::runtime_error);
  EXPECT_TRUE(this->map.erase(this->make_key(42)));
}

TYPED_TEST(VyukovHashMap, erase_unlocks_bucket_in_case_of_exception) {
  using hash_map =
    xenium::vyukov_hash_map<throwing_key, int, xenium::policy::reclaimer<typename TestFixture::Reclaimer>>;
  hash_map map;

  map.emplace(throwing_key{42}, 42);
  EXPECT_THROW(map.erase(throwing_key{42}), std::runtime_error);
  auto it = map.begin();
  EXPECT_EQ(42, (*it).first.v);
}
TYPED_TEST(VyukovHashMap, correctly_handles_hash_collisions) {
  using Key = typename TestFixture::KeyType;
  using Value = typename TestFixture::MapValueType;
  using Reclaimer = typename TestFixture::Reclaimer;
  struct dummy_hash {
    dummy_hash() = default;
    std::size_t operator()(const Key&) { return 1; }
  };
  using hash_map =
    xenium::vyukov_hash_map<Key, Value, xenium::policy::reclaimer<Reclaimer>, xenium::policy::hash<dummy_hash>>;
  hash_map map;

  EXPECT_TRUE(map.emplace(this->make_key(42), this->make_value(42)));
  EXPECT_TRUE(map.emplace(this->make_key(43), this->make_value(43)));
  typename hash_map::accessor acc;
  EXPECT_TRUE(map.try_get_value(this->make_key(42), acc));
  EXPECT_EQ(this->make_comparison_value(42), *acc);
  EXPECT_TRUE(map.try_get_value(this->make_key(43), acc));
  EXPECT_EQ(this->make_comparison_value(43), *acc);

  EXPECT_TRUE(map.extract(this->make_key(42), acc));
  EXPECT_EQ(this->make_comparison_value(42), *acc);
}

TYPED_TEST(VyukovHashMap, begin_returns_end_iterator_for_empty_map) {
  auto it = this->map.begin();
  ASSERT_EQ(this->map.end(), it);
}

TYPED_TEST(VyukovHashMap, begin_returns_iterator_to_first_entry) {
  this->map.emplace(this->make_key(42), this->make_value(43));
  auto it = this->map.begin();
  ASSERT_NE(this->map.end(), it);
  EXPECT_EQ(this->make_key(42), (*it).first);
  EXPECT_EQ(this->make_comparison_value(43), (*it).second);
  ++it;
  ASSERT_EQ(this->map.end(), it);
}

TYPED_TEST(VyukovHashMap, drain_densely_populated_map_using_erase) {
  for (int i = 0; i < 200; ++i) {
    this->map.emplace(this->make_key(i), this->make_value(i));
  }

  auto it = this->map.begin();
  while (it != this->map.end()) {
    this->map.erase(it);
  }

  EXPECT_EQ(this->map.end(), this->map.begin());
}

TYPED_TEST(VyukovHashMap, drain_sparsely_populated_map_using_erase) {
  for (int i = 0; i < 4; ++i) {
    this->map.emplace(this->make_key(i * 7), this->make_value(i));
  }

  auto it = this->map.begin();
  while (it != this->map.end()) {
    this->map.erase(it);
  }

  EXPECT_EQ(this->map.end(), this->map.begin());
}

TYPED_TEST(VyukovHashMap, iterator_covers_all_entries_in_densely_populated_map) {
  std::map<typename TestFixture::KeyType, bool> values;
  for (int i = 0; i < 200; ++i) {
    auto key = this->make_key(i);
    values[key] = false;
    this->map.emplace(key, this->make_value(i));
  }
  for (auto v : this->map) {
    values[v.first] = true;
  }
  for (auto& v : values) {
    EXPECT_TRUE(v.second) << v.first << " was not visited";
  }
}

TYPED_TEST(VyukovHashMap, iterator_covers_all_entries_in_sparsely_populated_map) {
  std::map<typename TestFixture::KeyType, bool> values;
  for (int i = 0; i < 4; ++i) {
    auto key = this->make_key(i * 7);
    values[key] = false;
    this->map.emplace(key, this->make_value(i));
  }
  for (auto v : this->map) {
    values[v.first] = true;
  }

  for (auto& v : values) {
    EXPECT_TRUE(v.second) << v.first << " was not visited";
  }
}

#ifdef DEBUG
const int MaxIterations = 2000;
#else
const int MaxIterations = 8000;
#endif

TYPED_TEST(VyukovHashMap, parallel_usage) {
  using Reclaimer = typename TestFixture::Reclaimer;
  static constexpr int keys_per_thread = 8;

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.push_back(std::thread([this, i] {
      for (int k = i * keys_per_thread; k < (i + 1) * keys_per_thread; ++k) {
        auto key = this->make_key(k);
        auto value = this->make_comparison_value(k);
        [[maybe_unused]] typename Reclaimer::region_guard guard{};
        for (int j = 0; j < MaxIterations / keys_per_thread; ++j) {
          [[maybe_unused]] typename Reclaimer::region_guard guard{};
          EXPECT_TRUE(this->map.emplace(key, this->make_value(k)));
          for (int x = 0; x < 10; ++x) {
            typename TestFixture::hash_map::accessor acc;
            EXPECT_TRUE(this->map.try_get_value(key, acc))
              << "k=" << k << ", j=" << j << ", x=" << x << ", thread=" << i;
            EXPECT_EQ(value, *acc) << "k=" << k << "j=" << j << ", x=" << x << ", thread=" << i;
          }
          if ((j + i) % 8 == 0) {
            for (auto it = this->map.begin(); it != this->map.end();) {
              if constexpr (std::is_same_v<typename TestFixture::KeyType, typename TestFixture::ValueType>) {
                EXPECT_EQ((*it).first, (*it).second);
              } else if constexpr (std::is_same_v<typename TestFixture::KeyType, int>) {
                EXPECT_EQ(this->make_comparison_value((*it).first), (*it).second);
              } else if constexpr (std::is_same_v<typename TestFixture::ValueType, int>) {
                EXPECT_EQ((*it).first, this->make_key((*it).second));
              }
              if ((*it).first == key) {
                this->map.erase(it);
              } else {
                ++it;
              }
            }
          } else if ((j + i) % 4 == 0) {
            typename TestFixture::hash_map::accessor acc;
            EXPECT_TRUE(this->map.extract(key, acc));
            EXPECT_EQ(value, *acc);
          } else {
            EXPECT_TRUE(this->map.erase(key));
          }
        }
      }
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TYPED_TEST(VyukovHashMap, parallel_usage_with_same_values) {
  using Reclaimer = typename TestFixture::Reclaimer;

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.push_back(std::thread([this, i] {
      for (int j = 0; j < MaxIterations / 10; ++j) {
        for (int k = 0; k < 10; ++k) {
          auto key = this->make_key(k);
          [[maybe_unused]] typename Reclaimer::region_guard guard{};
          this->map.emplace(key, this->make_value(k));
          typename TestFixture::hash_map::accessor acc;
          if (this->map.try_get_value(key, acc)) {
            EXPECT_EQ(this->make_comparison_value(k), *acc) << "j=" << j << ", thread=" << i;
          }

          if (j % 8 == 0) {
            // just iterate through the map, but don't really do anything
            for (auto v : this->map) {
              (void)v;
            }
          } else if (j % 4 == 0) {
            auto it = this->map.find(key);
            if (it != this->map.end()) {
              this->map.erase(it);
            }
          } else {
            this->map.erase(key);
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

#ifdef _MSC_VER
  #pragma warning(pop)
#endif
