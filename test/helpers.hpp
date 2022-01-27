#pragma once

namespace xenium::test {
struct non_default_constructible {
  explicit non_default_constructible(int v) : value(v) {}
  int const value;
};

struct non_default_constructible_assignable {
  explicit non_default_constructible_assignable(int v) : value(v) {}
  int value;
};

} // namespace xenium::test