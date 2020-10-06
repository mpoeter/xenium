//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_DETAL_GUARD_PTR_HPP
#define XENIUM_DETAL_GUARD_PTR_HPP

#include <utility>

namespace xenium::reclamation::detail {

template <class T, class MarkedPtr, class Derived>
class guard_ptr {
public:
  ~guard_ptr() { self().reset(); }

  // Get underlying pointer
  [[nodiscard]] T* get() const noexcept { return ptr.get(); }

  // Get mark bits
  [[nodiscard]] uintptr_t mark() const noexcept { return ptr.mark(); }

  operator MarkedPtr() const noexcept { return ptr; } // NOLINT (explicit)

  // True if get() != nullptr || mark() != 0
  explicit operator bool() const noexcept { return static_cast<bool>(ptr); }

  // Get pointer with mark bits stripped off. Undefined if target has been reclaimed.
  T* operator->() const noexcept { return ptr.get(); }

  // Get reference to target of pointer. Undefined if target has been reclaimed.
  T& operator*() const noexcept { return *ptr; }

  // Swap two guards
  void swap(Derived& g) noexcept {
    std::swap(ptr, g.ptr);
    self().do_swap(g);
  }

protected:
  // NOLINTNEXTLINE (explicit-constructor)
  guard_ptr(const MarkedPtr& p = MarkedPtr{}) noexcept : ptr(p) {}
  MarkedPtr ptr;

  void do_swap(Derived&) noexcept {} // empty dummy

private:
  Derived& self() { return static_cast<Derived&>(*this); }
};
} // namespace xenium::reclamation::detail

#endif
