#include <xenium/detail/nikolaev_scq.hpp>

#include <gtest/gtest.h>

#include <random>
#include <thread>
#include <vector>

namespace {

struct NikolaevSCQ : testing::Test {};

namespace {
  constexpr std::size_t capacity = 8;
  constexpr std::size_t remap_shift = xenium::detail::nikolaev_scq::calc_remap_shift(capacity);
} // namespace

TEST(NikolaevSCQ, construct_empty) {
  xenium::detail::nikolaev_scq queue(capacity, remap_shift, xenium::detail::nikolaev_scq::empty_tag{});
  std::uint64_t v;
  auto res = queue.dequeue<false, 0>(v, capacity, remap_shift);
  ASSERT_FALSE(res);
  for (std::size_t i = 0; i < 2 * capacity; ++i) {
    res = queue.enqueue<false, false>(i / 2, capacity, remap_shift);
    ASSERT_TRUE(res);
  }
}

TEST(NikolaevSCQ, construct_full) {
  xenium::detail::nikolaev_scq queue(capacity, remap_shift, xenium::detail::nikolaev_scq::full_tag{});
  std::uint64_t v;
  for (std::size_t i = 0; i < capacity; ++i) {
    auto res = queue.dequeue<false, false>(v, capacity, remap_shift);
    ASSERT_TRUE(res);
    EXPECT_EQ(i, v);
  }
  auto res = queue.dequeue<false, false>(v, capacity, remap_shift);
  ASSERT_FALSE(res);
}

TEST(NikolaevSCQ, construct_first_used) {
  xenium::detail::nikolaev_scq queue(capacity, remap_shift, xenium::detail::nikolaev_scq::first_used_tag{});
  std::uint64_t v;
  auto res = queue.dequeue<false, 0>(v, capacity, remap_shift);
  ASSERT_TRUE(res);
  ASSERT_EQ(0, v);
  res = queue.dequeue<false, 0>(v, capacity, remap_shift);
  ASSERT_FALSE(res);
}

TEST(NikolaevSCQ, construct_first_empty) {
  xenium::detail::nikolaev_scq queue(capacity, remap_shift, xenium::detail::nikolaev_scq::first_empty_tag{});
  auto res = queue.enqueue<false, 0>(0, capacity, remap_shift);
  ASSERT_TRUE(res);
  std::uint64_t v;
  for (std::size_t i = 0; i < capacity; ++i) {
    res = queue.dequeue<false, false>(v, capacity, remap_shift);
    ASSERT_TRUE(res);
    auto expected = (i + 1) % capacity;
    EXPECT_EQ(expected, v);
  }
}

} // namespace