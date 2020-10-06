#include <xenium/chase_work_stealing_deque.hpp>

#include <gtest/gtest.h>

#include <random>
#include <thread>
#include <vector>

namespace {

struct node {
  int v;
};

struct ChaseWorkStealingDeque : testing::Test {};

TEST(ChaseWorkStealingDeque, push_try_pop_returns_pushed_element) {
  auto n = std::make_unique<node>();
  xenium::chase_work_stealing_deque<node> queue;
  EXPECT_TRUE(queue.try_push(n.get()));
  node* elem;
  EXPECT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(n.get(), elem);
}

TEST(ChaseWorkStealingDeque, push_try_steal_returns_pushed_element) {
  auto n = std::make_unique<node>();
  xenium::chase_work_stealing_deque<node> queue;
  EXPECT_TRUE(queue.try_push(n.get()));
  node* elem;
  EXPECT_TRUE(queue.try_steal(elem));
  EXPECT_EQ(n.get(), elem);
}

TEST(ChaseWorkStealingDeque, push_two_items_steal_returns_them_in_FIFO_order) {
  auto n1 = std::make_unique<node>();
  auto n2 = std::make_unique<node>();

  xenium::chase_work_stealing_deque<node> queue;
  EXPECT_TRUE(queue.try_push(n1.get()));
  EXPECT_TRUE(queue.try_push(n2.get()));

  node* elem;
  EXPECT_TRUE(queue.try_steal(elem));
  EXPECT_EQ(n1.get(), elem);
  EXPECT_TRUE(queue.try_steal(elem));
  EXPECT_EQ(n2.get(), elem);
}

TEST(ChaseWorkStealingDeque, push_two_items_pop_returns_them_in_LIFO_order) {
  auto n1 = std::make_unique<node>();
  auto n2 = std::make_unique<node>();

  xenium::chase_work_stealing_deque<node> queue;
  EXPECT_TRUE(queue.try_push(n1.get()));
  EXPECT_TRUE(queue.try_push(n2.get()));

  node* elem;
  EXPECT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(n2.get(), elem);
  EXPECT_TRUE(queue.try_pop(elem));
  EXPECT_EQ(n1.get(), elem);
}

TEST(ChaseWorkStealingDeque, push_pop_steal_many) {
  constexpr unsigned count = 4000;
  auto n = std::make_unique<node>();
  xenium::chase_work_stealing_deque<node> queue;

  for (int x = 0; x < 3; ++x) {
    for (unsigned i = 0; i < count; ++i) {
      ASSERT_TRUE(queue.try_push(n.get()));
    }

    node* v;
    for (unsigned i = 0; i < count; ++i) {
      if (i % 2 == 0) {
        ASSERT_TRUE(queue.try_pop(v));
      } else {
        ASSERT_TRUE(queue.try_steal(v));
      }
    }
  }
}

TEST(ChaseWorkStealingDeque, parallel_usage) {
  constexpr unsigned num_threads = 8;
  constexpr unsigned num_nodes = num_threads * 8;
#ifdef DEBUG
  constexpr int MaxIterations = 100000;
#else
  constexpr int MaxIterations = 1000000;
#endif

  xenium::chase_work_stealing_deque<node> queues[num_threads];
  std::unique_ptr<node> nodes[num_nodes];
  std::thread threads[num_threads];

  for (unsigned i = 0; i < num_nodes; ++i) {
    nodes[i] = std::make_unique<node>();
    nodes[i]->v = 1;
    queues[i % num_threads].try_push(nodes[i].get());
  }

  std::atomic<bool> start{false};

  for (unsigned i = 0; i < num_threads; ++i) { // NOLINT (modernize-loop-convert)
    threads[i] = std::thread([thread_idx = i, &start, &queues, num_threads, MaxIterations]() {
      // oh my... MSVC complains if these variables are NOT captured; clang complains if they ARE captured.
      (void)num_threads;
      (void)MaxIterations;

      std::mt19937 rand;
      rand.seed(thread_idx);
      node* n = nullptr;

      while (!start.load()) {
        ; // wait for start signal
      }

      for (int j = 0; j < MaxIterations; ++j) {
        if (n != nullptr) {
          ++n->v;
          EXPECT_EQ(1, n->v);
          EXPECT_TRUE(queues[thread_idx].try_push(n));
          n = nullptr;
        } else {
          auto idx = rand() % num_threads;
          if (idx == thread_idx && queues[thread_idx].size() > 0) {
            if (queues[idx].try_pop(n)) {
              EXPECT_NE(nullptr, n);
              --n->v;
              EXPECT_EQ(0, n->v);
            }
          } else {
            if (queues[idx].try_steal(n)) {
              EXPECT_NE(nullptr, n);
              --n->v;
              EXPECT_EQ(0, n->v);
            }
          }
        }
      }

      if (n != nullptr) {
        ++n->v;
        queues[thread_idx].try_push(n);
      }
    });
  }

  start.store(true);

  for (auto& thread : threads) {
    thread.join();
  }
}
} // namespace