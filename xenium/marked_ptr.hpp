//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_MARKED_PTR_HPP
#define XENIUM_MARKED_PTR_HPP

#include <xenium/utils.hpp>

#include <cassert>
#include <cstdint>
#include <cstddef>

#ifndef XENIUM_MAX_UPPER_MARK_BITS
  #define XENIUM_MAX_UPPER_MARK_BITS 16
#endif

#ifdef _MSC_VER
#pragma warning(push)
// TODO - remove this after upgrading to C++17
#pragma warning(disable: 4127) // conditional expression is constant
#pragma warning(disable: 4293) // shift count negative or too big
#endif

namespace xenium {
  /**
   * @brief A pointer with an embedded mark/tag value.
   *
   * Acts like a pointer, but has an embeded mark/tag value with `MarkBits` bits.
   * On most systems pointers are only 48-bit, leaving the 16 topmost bits clear.
   * Therefore, the mark value is usually embeded in these top-most bits. For mark
   * mark values that exceed `MaxUpperMarkBits` bits the remaining bits are embedded
   * in the lowest bits, but this requires the pointer to be aligned properly.
   * 
   * @tparam T
   * @tparam MarkBits the number of bits used for the mark value
   * @tparam MaxUpperMarkBits the max number of bits to be used (defaults to 16)
   */
  template <class T, uintptr_t MarkBits, uintptr_t MaxUpperMarkBits = XENIUM_MAX_UPPER_MARK_BITS>
  class marked_ptr {
    static_assert(MarkBits > 0, "should never happen - compiler should pick the specilization for zero MarkBits!");
    static constexpr uintptr_t pointer_bits = sizeof(T*) * 8 - MarkBits;
    static constexpr uintptr_t MarkMask = (static_cast<uintptr_t>(1) << MarkBits) - 1;

    static constexpr uintptr_t lower_mark_bits = MarkBits < MaxUpperMarkBits ? 0 : MarkBits - MaxUpperMarkBits;
    static constexpr uintptr_t upper_mark_bits = MarkBits - lower_mark_bits;
    static constexpr uintptr_t pointer_mask = ((static_cast<uintptr_t>(1) << pointer_bits) - 1) << lower_mark_bits;
    
  public:
    static constexpr uintptr_t number_of_mark_bits = MarkBits;
    static_assert(MarkBits <= 32, "MarkBits must not be greater than 32.");
    static_assert(sizeof(T*) == 8, "marked_ptr requires 64bit pointers.");

    /**
     * @brief Construct a marked ptr with an optional mark value.
     * 
     * The `mark` value is automatically trimmed to `MarkBits` bits.
     */
    marked_ptr(T* p = nullptr, uintptr_t mark = 0) noexcept : ptr(make_ptr(p, mark)) {}
    
    /**
     * @brief Reset the pointer to `nullptr` and the mark to 0.
     */
    void reset() noexcept { ptr = nullptr; }
    
    /**
     * @brief Get the mark value.
     */
    uintptr_t mark() const noexcept {
      return utils::rotate<lower_mark_bits>::right(reinterpret_cast<uintptr_t>(ptr)) >> pointer_bits;
    }
    
    /**
     * @brief Get underlying pointer (with mark bits stripped off).
     */
    T* get() const noexcept {
      auto ip = reinterpret_cast<uintptr_t>(ptr);
      if constexpr(number_of_mark_bits != 0)
        ip &= pointer_mask;
      return reinterpret_cast<T*>(ip);
    }
    
    /**
     * @brief True if `get() != nullptr || mark() != 0`
     */
    explicit operator bool() const noexcept { return ptr != nullptr; }
    
    /**
     * @brief Get pointer with mark bits stripped off.
     */
    T* operator->() const noexcept { return get(); }
    
    /**
     * @brief Get reference to target of pointer.
     */
    T& operator*() const noexcept { return *get(); }

    inline friend bool operator==(const marked_ptr& l, const marked_ptr& r) { return l.ptr == r.ptr; }
    inline friend bool operator!=(const marked_ptr& l, const marked_ptr& r) { return l.ptr != r.ptr; }

  private:
    T* make_ptr(T* p, uintptr_t mark) noexcept {
      assert((reinterpret_cast<uintptr_t>(p) & ~pointer_mask) == 0 &&
        "bits reserved for masking are occupied by the pointer");

      uintptr_t ip = reinterpret_cast<uintptr_t>(p);
      if constexpr (number_of_mark_bits == 0) {
        assert(mark == 0);
        return p;
      }
      else {
        mark = utils::rotate<lower_mark_bits>::left(mark << pointer_bits);
        return reinterpret_cast<T*>(ip | mark);
      }
    }

    T* ptr;

#ifdef _MSC_VER
    // These members are only for the VS debugger visualizer (natvis).
    enum Masking { MarkMask_ = MarkMask };
    using PtrType = T*;
#endif
  };

  template <class T, uintptr_t MaxUpperMarkBits>
  class marked_ptr<T, 0, MaxUpperMarkBits> {
  public:
    static constexpr uintptr_t number_of_mark_bits = 0;
    
    /**
     * @brief Construct a marked ptr.
     */
    marked_ptr(T* p = nullptr) noexcept {
      ptr = p;
    }
    
    /**
     * @brief Reset the pointer to `nullptr` and the mark to 0.
     */
    void reset() noexcept { ptr = nullptr; }
    
    /**
     * @brief Get the mark value.
     */
    uintptr_t mark() const noexcept { return 0; }
    
    /**
     * @brief Get underlying pointer.
     */
    T* get() const noexcept { return ptr; }
    
    /**
     * @brief True if `get() != nullptr || mark() != 0`
     */
    explicit operator bool() const noexcept { return ptr != nullptr; }
    
    /**
     * @brief Get pointer with mark bits stripped off.
     */
    T* operator->() const noexcept { return get(); }
    
    /**
     * @brief Get reference to target of pointer.
     */
    T& operator*() const noexcept { return *get(); }

    inline friend bool operator==(const marked_ptr& l, const marked_ptr& r) { return l.ptr == r.ptr; }
    inline friend bool operator!=(const marked_ptr& l, const marked_ptr& r) { return l.ptr != r.ptr; }

  private:
    T* ptr;
  };
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif