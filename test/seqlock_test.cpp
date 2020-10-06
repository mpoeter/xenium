#include <xenium/seqlock.hpp>

#include <gtest/gtest.h>
#include <thread>

namespace {

struct Foo {
  int32_t v1 = 0;
  float v2 = 0;
  double v3 = 0;
  int64_t v4 = 0;

  Foo& operator++() {
    ++v1;
    ++v2; // NOLINT
    ++v3;
    ++v4;
    return *this;
  }

  [[nodiscard]] bool verify() const { return v1 == v2 && v2 == v3 && v3 == v4; } // NOLINT
  bool operator==(const Foo& rhs) const { return v1 == rhs.v1 && v2 == rhs.v2 && v3 == rhs.v3 && v4 == rhs.v4; } // NOLINT
};

TEST(SeqLock, load_returns_initial_value) {
  xenium::seqlock<Foo> data{{0, 1, 2, 3}};
  Foo expected = {0, 1, 2, 3};
  EXPECT_EQ(expected, data.load());
}

TEST(SeqLock, load_returns_previously_stored_value) {
  Foo f = {0, 1, 2, 3};
  xenium::seqlock<Foo> data{};
  for (int32_t i = 0; i < 4; ++i) {
    EXPECT_EQ(i, f.v1);
    data.store(f);
    EXPECT_EQ(f, data.load());
    ++f;
  }
}

TEST(SeqLock, load_returns_previously_stored_value_with_multiple_slots) {
  Foo f = {0, 1, 2, 3};
  xenium::seqlock<Foo, xenium::policy::slots<8>> data{};
  for (int32_t i = 0; i < 8; ++i) {
    EXPECT_EQ(i, f.v1);
    data.store(f);
    EXPECT_EQ(f, data.load());
    ++f;
  }
}

TEST(SeqLock, update_functor_receives_latest_value_as_parameter) {
  Foo f = {0, 1, 2, 3};
  xenium::seqlock<Foo> data{f};
  for (int32_t i = 0; i < 4; ++i) {
    EXPECT_EQ(i, f.v1);
    data.update([&f](Foo& cur) {
      EXPECT_EQ(f, cur);
      ++cur;
    });
    ++f;
  }
}

TEST(SeqLock, update_functor_receives_latest_value_as_parameter_with_multple_slots) {
  Foo f = {0, 1, 2, 3};
  xenium::seqlock<Foo, xenium::policy::slots<4>> data{f};
  for (int32_t i = 0; i < 8; ++i) {
    EXPECT_EQ(i, f.v1);
    data.update([&f](Foo& cur) {
      EXPECT_EQ(f, cur);
      ++cur;
    });
    ++f;
  }
}

TEST(SeqLock, read_returns_value_stored_by_update) {
  Foo f = {0, 1, 2, 3};
  xenium::seqlock<Foo> data{f};
  for (int32_t i = 0; i < 4; ++i) {
    EXPECT_EQ(i, f.v1);
    data.update([](Foo& cur) { ++cur; });
    ++f;
    EXPECT_EQ(f, data.load());
  }
}

TEST(SeqLock, read_returns_value_stored_by_update_with_multiple_slots) {
  Foo f = {0, 1, 2, 3};
  xenium::seqlock<Foo, xenium::policy::slots<4>> data{f};
  for (int32_t i = 0; i < 9; ++i) {
    EXPECT_EQ(i, f.v1);
    data.update([](Foo& cur) { ++cur; });
    ++f;
    EXPECT_EQ(f, data.load());
  }
}

TEST(SeqLock, parallel_usage) {
  xenium::seqlock<Foo, xenium::policy::slots<2>> data{{0, 0, 0, 0}};

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&data, i] {
#ifdef DEBUG
      const int MaxIterations = 5000;
#else
      const int MaxIterations = 50000;
#endif
      for (int j = 0; j < MaxIterations; ++j) {
        auto d = data.load();
        EXPECT_TRUE(d.verify());
        data.store(++d);

        d = data.load();
        EXPECT_TRUE(d.verify());
        if (i < 2) {
          data.update([](Foo& f) {
            EXPECT_TRUE(f.verify());
            ++f;
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
