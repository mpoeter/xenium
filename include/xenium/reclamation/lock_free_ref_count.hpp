//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_LOCK_FREE_REF_COUNT_HPP
#define XENIUM_LOCK_FREE_REF_COUNT_HPP

#include <xenium/reclamation/detail/allocation_tracker.hpp>
#include <xenium/reclamation/detail/concurrent_ptr.hpp>
#include <xenium/reclamation/detail/guard_ptr.hpp>

#include <xenium/acquire_guard.hpp>
#include <xenium/parameter.hpp>

#include <memory>

namespace xenium {

namespace policy {
  /**
   * @brief Policy to configure whether to insert padding after the internal header for
   * `lock_free_ref_count` reclamation.
   *
   * This policy is used to define whether a padding should be inserted between the internal
   * header that contains the reference counter and the actual object. This can be used to
   * avoid false sharing, but of course it increases memory overhead, which can also cause
   * a performance drop in some cases.
   *
   * @tparam Value
   */
  template <bool Value>
  struct insert_padding;

  /**
   * @brief Policy to configure the size of thread-local free-lists for `lock` reclamation.
   *
   * This policy is used to define the max. number of items in each thread-local free-list.
   * If this is set to zero, then thread-local free-lists are completely disable.
   * Using thread-local free-lists cna reduce the contention on the global free-list, but it
   * may lead to increased memory usage, since items stored in a thread-local free-list can
   * only be reused by the owning thread.
   *
   * @tparam Value
   */
  template <std::size_t Value>
  struct thread_local_free_list_size;
} // namespace policy

namespace reclamation {
  template <bool InsertPadding = false, std::size_t ThreadLocalFreeListSize = 0>
  struct lock_free_ref_count_traits {
    static constexpr bool insert_padding = InsertPadding;
    static constexpr std::size_t thread_local_free_list_size = ThreadLocalFreeListSize;

    template <class... Policies>
    using with = lock_free_ref_count_traits<
      parameter::value_param_t<bool, policy::insert_padding, InsertPadding, Policies...>::value,
      parameter::value_param_t<std::size_t, policy::thread_local_free_list_size, ThreadLocalFreeListSize, Policies...>::
        value>;
  };

  /**
   * @brief An implementation of the lock-free reference counting (LFRC) schemea as proposed
   * by Valois \[[Val95](index.html#ref-valois-1995), [MS95](index.html#ref-michael-1995)\].
   *
   * This scheme cannot handle types that define their own new/delete operators, and it
   * does not allow the use of custom deleters.
   *
   * This class does not take a list of policies, but a `Traits` type that can be customized
   * with a list of policies. The following policies are supported:
   *  * `xenium::policy::insert_padding`<br>
   *    Defines whether a padding should be inserted between the internal header and the actual
   *    object. (defaults to false)
   *  * `xenium::policy::thread_local_free_list_size`<br>
   *    Defines the max. number of items in each thread-local free-list. (defaults to 0)
   *
   * @tparam Traits
   */
  template <class Traits = lock_free_ref_count_traits<>>
  class lock_free_ref_count {
    template <class T, class MarkedPtr>
    class guard_ptr;

  public:
    template <class... Policies>
    using with = lock_free_ref_count<typename Traits::template with<Policies...>>;

    template <class T, std::size_t N = T::number_of_mark_bits>
    using concurrent_ptr = detail::concurrent_ptr<T, N, guard_ptr>;

    template <class T, std::size_t N = 0, class DeleterT = std::default_delete<T>>
    class enable_concurrent_ptr;

    class region_guard {};

    ALLOCATION_TRACKER
  private:
    static constexpr unsigned RefCountInc = 2;
    static constexpr unsigned RefCountClaimBit = 1;

    ALLOCATION_TRACKING_FUNCTIONS;
#ifdef TRACK_ALLOCATIONS
    inline static thread_local detail::registered_allocation_counter<lock_free_ref_count> allocation_counter_;
    static detail::allocation_counter& allocation_counter();
#endif
  };

  template <class Traits>
  template <class T, std::size_t N, class DeleterT>
  class lock_free_ref_count<Traits>::enable_concurrent_ptr : private detail::tracked_object<lock_free_ref_count> {
  public:
    enable_concurrent_ptr(const enable_concurrent_ptr&) noexcept = delete;
    enable_concurrent_ptr(enable_concurrent_ptr&&) noexcept = delete;
    enable_concurrent_ptr& operator=(const enable_concurrent_ptr&) noexcept = delete;
    enable_concurrent_ptr& operator=(enable_concurrent_ptr&&) noexcept = delete;

  protected:
    enable_concurrent_ptr() noexcept { destroyed().store(false, std::memory_order_relaxed); }
    virtual ~enable_concurrent_ptr() noexcept {
      assert(!is_destroyed());
      destroyed().store(true, std::memory_order_relaxed);
    }

  public:
    using Deleter = DeleterT;
    static_assert(std::is_same<Deleter, std::default_delete<T>>::value,
                  "lock_free_ref_count reclamation can only be used with std::default_delete as Deleter.");

    static constexpr std::size_t number_of_mark_bits = N;
    [[nodiscard]] unsigned refs() const { return getHeader()->ref_count.load(std::memory_order_relaxed) >> 1; }

    void* operator new(size_t sz);
    void operator delete(void* p);

  private:
    bool decrement_refcnt();
    [[nodiscard]] bool is_destroyed() const { return getHeader()->destroyed.load(std::memory_order_relaxed); }
    void push_to_free_list() { global_free_list.push(static_cast<T*>(this)); }

    struct unpadded_header {
      std::atomic<unsigned> ref_count;
      std::atomic<bool> destroyed;
      concurrent_ptr<T, N> next_free;
    };
    struct padded_header : unpadded_header {
      char padding[64 - sizeof(unpadded_header)];
    };
    using header = std::conditional_t<Traits::insert_padding, padded_header, unpadded_header>;
    header* getHeader() { return static_cast<header*>(static_cast<void*>(this)) - 1; }
    [[nodiscard]] const header* getHeader() const {
      return static_cast<const header*>(static_cast<const void*>(this)) - 1;
    }

    std::atomic<unsigned>& ref_count() { return getHeader()->ref_count; }
    std::atomic<bool>& destroyed() { return getHeader()->destroyed; }
    concurrent_ptr<T, N>& next_free() { return getHeader()->next_free; }

    friend class lock_free_ref_count;

    using guard_ptr = typename concurrent_ptr<T, N>::guard_ptr;
    using marked_ptr = typename concurrent_ptr<T, N>::marked_ptr;

    class free_list;
    static free_list global_free_list;
  };

  template <class Traits>
  template <class T, class MarkedPtr>
  class lock_free_ref_count<Traits>::guard_ptr : public detail::guard_ptr<T, MarkedPtr, guard_ptr<T, MarkedPtr>> {
    using base = detail::guard_ptr<T, MarkedPtr, guard_ptr>;
    using Deleter = typename T::Deleter;

  public:
    template <class, std::size_t, class>
    friend class enable_concurrent_ptr;

    // Guard a marked ptr.
    explicit guard_ptr(const MarkedPtr& p = MarkedPtr()) noexcept;
    guard_ptr(const guard_ptr& p) noexcept;
    guard_ptr(guard_ptr&& p) noexcept;

    guard_ptr& operator=(const guard_ptr& p);
    guard_ptr& operator=(guard_ptr&& p) noexcept;

    // Atomically take snapshot of p, and *if* it points to unreclaimed object, acquire shared ownership of it.
    void acquire(const concurrent_ptr<T>& p, std::memory_order order = std::memory_order_seq_cst) noexcept;

    // Like acquire, but quit early if a snapshot != expected.
    bool acquire_if_equal(const concurrent_ptr<T>& p,
                          const MarkedPtr& expected,
                          std::memory_order order = std::memory_order_seq_cst) noexcept;

    // Release ownership. Postcondition: get() == nullptr.
    void reset() noexcept;

    // Reset. Deleter d will be applied some time after all owners release their ownership.
    void reclaim(Deleter d = Deleter()) noexcept;
  };
} // namespace reclamation
} // namespace xenium

#define LOCK_FREE_REF_COUNT_IMPL
#include <xenium/reclamation/impl/lock_free_ref_count.hpp>
#undef LOCK_FREE_REF_COUNT_IMPL

#endif
