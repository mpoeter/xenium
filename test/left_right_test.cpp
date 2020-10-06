#include <xenium/left_right.hpp>

#include <gtest/gtest.h>

#include <random>
#include <thread>
#include <vector>

namespace {

TEST(LeftRight, read_provides_initial_value) {
  xenium::left_right<int> lr{42};
  lr.read([](int v) { ASSERT_EQ(42, v); });
}

TEST(LeftRight, read_can_return_value) {
  xenium::left_right<int> lr{42};
  auto v = lr.read([](int v) { return v; });
  ASSERT_EQ(42, v);
}

TEST(LeftRight, read_provides_updated_value) {
  xenium::left_right<int> lr{0};
  lr.update([](int& v) { v = 42; });
  lr.read([](int v) { ASSERT_EQ(42, v); });
  lr.update([](int& v) { ++v; });
  lr.read([](int v) { ASSERT_EQ(43, v); });
}

TEST(LeftRight, parallel_usage) {
  constexpr int MaxIterations = 8000;

  xenium::left_right<int> lr{0};

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([i, &lr, MaxIterations] {
      // oh my... MSVC complains if this variable is NOT captured; clang complains if it IS captured.
      (void)MaxIterations;

      std::mt19937 rand;
      rand.seed(i);

      for (int j = 0; j < MaxIterations; ++j) {
        int last_value = 0;
        if (rand() % 32 == 0) {
          lr.update([](int& v) { ++v; });
        } else {
          lr.read([&last_value](const int& v) {
            EXPECT_GE(v, last_value);
            last_value = v;
          });
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
}
} // namespace
