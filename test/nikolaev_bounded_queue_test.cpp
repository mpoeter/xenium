#include <xenium/nikolaev_bounded_queue.hpp>

#include <gtest/gtest.h>

#include <random>
#include <thread>
#include <vector>

namespace {

struct NikolaevBoundedQueue : testing::Test {};

struct non_default_constructible {
  explicit non_default_constructible(int x): x(x) {}
  int x;
};

TEST(NikolaevBoundedQueue, push_try_pop_returns_pushed_element)
{
  xenium::nikolaev_bounded_queue<int> queue(2);
  EXPECT_TRUE(queue.try_push(42));
  int elem;
  ASSERT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(42, elem);
}

TEST(NikolaevBoundedQueue, push_two_items_pop_them_in_FIFO_order)
{
  xenium::nikolaev_bounded_queue<int> queue(2);
  EXPECT_TRUE(queue.try_push(42));
  EXPECT_TRUE(queue.try_push(43));
  int elem1;
  int elem2;
  EXPECT_TRUE(queue.try_pop(elem1));
  ASSERT_TRUE(queue.try_pop(elem2));
  EXPECT_EQ(42, elem1);
  EXPECT_EQ(43, elem2);
}

TEST(NikolaevBoundedQueue, try_pop_returns_false_when_queue_is_empty)
{
  xenium::nikolaev_bounded_queue<int> queue(2);
  int elem;
  EXPECT_FALSE(queue.try_pop(elem));
}

TEST(NikolaevBoundedQueue, try_push_returns_false_when_queue_is_full)
{
  xenium::nikolaev_bounded_queue<int> queue(2);
  EXPECT_TRUE(queue.try_push(42));
  EXPECT_TRUE(queue.try_push(43));
  EXPECT_FALSE(queue.try_push(44));
}

TEST(NikolaevBoundedQueue, supports_move_only_types)
{
  xenium::nikolaev_bounded_queue<std::pair<int, std::unique_ptr<int>>> queue(2);
  queue.try_push({41, std::unique_ptr<int>(new int(42))});

  std::pair<int, std::unique_ptr<int>> elem;
  ASSERT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(41, elem.first);
  ASSERT_NE(nullptr, elem.second);
  EXPECT_EQ(42, *elem.second);
}


TEST(NikolaevBoundedQueue, supports_non_default_constructible_types)
{
  xenium::nikolaev_bounded_queue<non_default_constructible> queue(2);
  queue.try_push(non_default_constructible(42));

  non_default_constructible elem(0);
  ASSERT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(42, elem.x);
}

TEST(NikolaevBoundedQueue, push_pop_in_fifo_order_with_remapped_indexes)
{
  constexpr int capacity = 32;
  xenium::nikolaev_bounded_queue<int> queue(capacity);
  for (int i = 0; i < capacity; ++i)
    ASSERT_TRUE(queue.try_push(i));

  for (int i = 0; i < capacity; ++i) {
    int value;
    ASSERT_TRUE(queue.try_pop(value));
    EXPECT_EQ(i, value);
  }
}

#ifdef DEBUG
  const int MaxIterations = 40000;
#else
  const int MaxIterations = 400000;
#endif

TEST(NikolaevBoundedQueue, parallel_usage)
{
  xenium::nikolaev_bounded_queue<int> queue(8);

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
  {
    threads.push_back(std::thread([i, &queue]
    {
      for (int j = 0; j < MaxIterations; ++j)
      {
        EXPECT_TRUE(queue.try_push(i));
        int elem = 0;
        ASSERT_TRUE(queue.try_pop(elem));
        EXPECT_TRUE(elem >= 0 && elem <= 4);
      }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

TEST(NikolaevBoundedQueue, parallel_usage_mostly_full)
{
  xenium::nikolaev_bounded_queue<int> queue(8);
  for (int i = 0; i < 8; ++i) {
    queue.try_push(1);
  }

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
  {
    threads.push_back(std::thread([i, &queue]
    {
      std::mt19937_64 rand;
      rand.seed(i);
      
      for (int j = 0; j < MaxIterations; ++j)
      {
        if (rand() % 128 < 64) {
          queue.try_push(i);
        } else {
          int elem;
          if (queue.try_pop(elem)) {
            EXPECT_TRUE(elem >= 0 && elem <= 4);
          }
        }
      }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

TEST(NikolaevBoundedQueue, parallel_usage_mostly_empty)
{
  xenium::nikolaev_bounded_queue<int> queue(8);

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
  {
    threads.push_back(std::thread([i, &queue]
    {
      std::mt19937_64 rand;
      rand.seed(i);
      
      for (int j = 0; j < MaxIterations; ++j)
      {
        if (rand() % 128 < 16) {
          queue.try_push(i);
        } else {
          int elem;
          if (queue.try_pop(elem)) {
            EXPECT_TRUE(elem >= 0 && elem <= 4);
          }
        }
      }
    }));
  }

  for (auto& thread : threads)
    thread.join();
}

}