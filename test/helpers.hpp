#pragma once

namespace xenium::test {
struct non_default_constructible {
  explicit non_default_constructible(int v) : value(v) {}
  int const value;
};
} // namespace xenium::test