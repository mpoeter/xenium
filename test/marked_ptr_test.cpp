#include <xenium/marked_ptr.hpp>

#include <gtest/gtest.h>

namespace {

struct Foo {
  int x;
  [[maybe_unused]] static constexpr int number_of_mark_bits = 0;
};

TEST(marked_ptr, get_returns_correct_pointer) {
  Foo f;
  xenium::marked_ptr<Foo, 2> p(&f, 3);
  EXPECT_EQ(&f, p.get());

  xenium::marked_ptr<Foo, 18> p2(&f, (1 << 18) - 1);
  EXPECT_EQ(&f, p2.get());
}

TEST(marked_ptr, mark_returns_correct_value) {
  Foo f;

  xenium::marked_ptr<Foo, 2> p(&f, 3);
  EXPECT_EQ(3, p.mark());

  auto mark = (1 << 18) - 1;
  xenium::marked_ptr<Foo, 18> p2(&f, mark);
  EXPECT_EQ(mark, p2.mark());
}

TEST(marked_ptr, deref_works_correctly) {
  Foo f;
  xenium::marked_ptr<Foo, 2> p(&f, 3);
  ASSERT_EQ(&f, p.get());

  p->x = 42;
  EXPECT_EQ(42, f.x);

  (*p).x = 43;
  EXPECT_EQ(43, f.x);
}

TEST(marked_ptr, reset_sets_ptr_to_null) {
  Foo f;
  xenium::marked_ptr<Foo, 2> p(&f, 3);
  p.reset();
  EXPECT_EQ(nullptr, p.get());
}

} // namespace
