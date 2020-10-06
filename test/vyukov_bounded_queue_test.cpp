#include <xenium/vyukov_bounded_queue.hpp>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace {

struct VyukovBoundedQueue : testing::Test {};

TEST(VyukovBoundedQueue, push_try_pop_returns_pushed_element) {
  xenium::vyukov_bounded_queue<int> queue(2);
  static_assert(!xenium::vyukov_bounded_queue<int>::default_to_weak);
  EXPECT_TRUE(queue.try_push(42));
  int elem;
  EXPECT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(42, elem);
}

TEST(VyukovBoundedQueue, push_try_pop_weak_returns_pushed_element) {
  xenium::vyukov_bounded_queue<int> queue(2);
  EXPECT_TRUE(queue.try_push(42));
  int elem;
  EXPECT_TRUE(queue.try_pop_weak(elem));
  EXPECT_EQ(42, elem);
}

TEST(VyukovBoundedQueue, push_two_items_pop_them_in_FIFO_order) {
  xenium::vyukov_bounded_queue<int> queue(2);
  EXPECT_TRUE(queue.try_push(42));
  EXPECT_TRUE(queue.try_push(43));
  int elem1;
  int elem2;
  EXPECT_TRUE(queue.try_pop(elem1));
  EXPECT_TRUE(queue.try_pop(elem2));
  EXPECT_EQ(42, elem1);
  EXPECT_EQ(43, elem2);
}

TEST(VyukovBoundedQueue, try_pop_returns_false_when_queue_is_empty) {
  xenium::vyukov_bounded_queue<int> queue(2);
  int elem;
  EXPECT_FALSE(queue.try_pop(elem));
  EXPECT_FALSE(queue.try_pop_weak(elem));
}

TEST(VyukovBoundedQueue, try_push_returns_false_when_queue_is_full) {
  xenium::vyukov_bounded_queue<int> queue(2);
  EXPECT_TRUE(queue.try_push(42));
  EXPECT_TRUE(queue.try_push(43));
  EXPECT_FALSE(queue.try_push(44));
  EXPECT_FALSE(queue.try_push_weak(44));
}

TEST(VyukovBoundedQueue, supports_move_only_types) {
  xenium::vyukov_bounded_queue<std::pair<int, std::unique_ptr<int>>> queue(2);
  queue.try_push(41, std::make_unique<int>(42));

  std::pair<int, std::unique_ptr<int>> elem;
  ASSERT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(41, elem.first);
  ASSERT_NE(nullptr, elem.second);
  EXPECT_EQ(42, *elem.second);
}

TEST(VyukovBoundedQueue, parallel_usage) {
  xenium::vyukov_bounded_queue<int> queue(8);

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([i, &queue] {
#ifdef DEBUG
      const int MaxIterations = 40000;
#else
      const int MaxIterations = 400000;
#endif
      for (int j = 0; j < MaxIterations; ++j) {
        EXPECT_TRUE(queue.try_push(i));
        int elem = 0;
        EXPECT_TRUE(queue.try_pop(elem));
        EXPECT_TRUE(elem >= 0 && elem <= 4);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST(VyukovBoundedQueue, parallel_usage_of_weak_operations) {
  xenium::vyukov_bounded_queue<int> queue(8);

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([i, &queue] {
#ifdef DEBUG
      const int MaxIterations = 40000;
#else
      const int MaxIterations = 400000;
#endif
      for (int j = 0; j < MaxIterations; ++j) {
        queue.try_push_weak(i);
        int elem;
        if (queue.try_pop_weak(elem)) {
          EXPECT_TRUE(elem >= 0 && elem <= 4);
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

} // namespace