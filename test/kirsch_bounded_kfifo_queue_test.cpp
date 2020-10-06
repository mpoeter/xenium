#include <xenium/kirsch_bounded_kfifo_queue.hpp>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace {

int* v1 = new int(42);
int* v2 = new int(43);

struct KirschBoundedKFifoQueue : testing::Test {};

TEST(KirschBoundedKFifoQueue, push_try_pop_returns_pushed_element) {
  xenium::kirsch_bounded_kfifo_queue<int*> queue(1, 2);
  EXPECT_TRUE(queue.try_push(v1));
  int* elem = nullptr;
  EXPECT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(v1, elem);
}

TEST(KirschBoundedKFifoQueue, push_two_items_pop_them_in_FIFO_order) {
  xenium::kirsch_bounded_kfifo_queue<int*> queue(1, 2);
  EXPECT_TRUE(queue.try_push(v1));
  EXPECT_TRUE(queue.try_push(v2));
  int* elem1 = nullptr;
  int* elem2 = nullptr;
  EXPECT_TRUE(queue.try_pop(elem1));
  EXPECT_TRUE(queue.try_pop(elem2));
  EXPECT_EQ(v1, elem1);
  EXPECT_EQ(v2, elem2);
}

TEST(KirschBoundedKFifoQueue, try_pop_returns_false_when_queue_is_empty) {
  xenium::kirsch_bounded_kfifo_queue<int*> queue(1, 2);
  int* elem;
  EXPECT_FALSE(queue.try_pop(elem));
}

TEST(KirschBoundedKFifoQueue, try_push_returns_false_when_queue_is_full) {
  xenium::kirsch_bounded_kfifo_queue<int*> queue(1, 2);
  EXPECT_TRUE(queue.try_push(v1));
  EXPECT_TRUE(queue.try_push(v2));
  EXPECT_FALSE(queue.try_push(v2));
}

TEST(KirschBoundedKFifoQueue, supports_unique_ptr) {
  xenium::kirsch_bounded_kfifo_queue<std::unique_ptr<int>> queue(1, 2);
  auto elem = std::make_unique<int>(42);
  auto* p = elem.get();
  EXPECT_TRUE(queue.try_push(std::move(elem)));
  ASSERT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(p, elem.get()); // NOLINT (use-after-move)
  EXPECT_EQ(42, *elem); // NOLINT (use-after-move)
}

TEST(KirschBoundedKFifoQueue, deletes_remaining_unique_ptr_entries) {
  unsigned delete_count = 0;
  struct dummy {
    unsigned& delete_count;
    explicit dummy(unsigned& delete_count) : delete_count(delete_count) {}
    ~dummy() { ++delete_count; }
  };
  {
    xenium::kirsch_bounded_kfifo_queue<std::unique_ptr<dummy>> queue(1, 101);
    for (int i = 0; i < 100; ++i) {
      EXPECT_TRUE(queue.try_push(std::make_unique<dummy>(delete_count)));
      EXPECT_TRUE(queue.try_push(std::make_unique<dummy>(delete_count)));
      std::unique_ptr<dummy> elem;
      EXPECT_TRUE(queue.try_pop(elem));
    }
  }
  EXPECT_EQ(200u, delete_count);
}

TEST(KirschBoundedKFifoQueue, parallel_usage) {
  constexpr int max_threads = 8;
  xenium::kirsch_bounded_kfifo_queue<int*> queue(1, max_threads);

  std::vector<std::thread> threads;
  for (int i = 0; i < max_threads; ++i) {
    threads.emplace_back([i, &queue, max_threads] {
      // oh my... MSVC complains if this variable is NOT captured; clang complains if it IS captured.
      (void)max_threads;

#ifdef DEBUG
      const int MaxIterations = 10000;
#else
      const int MaxIterations = 100000;
#endif
      for (int j = 0; j < MaxIterations; ++j) {
        EXPECT_TRUE(queue.try_push(new int(i)));
        int* elem = nullptr;
        EXPECT_TRUE(queue.try_pop(elem));
        EXPECT_TRUE(*elem >= 0 && *elem <= max_threads);
        delete elem;
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

} // namespace