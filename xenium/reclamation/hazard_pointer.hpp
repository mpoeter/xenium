//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_HAZARD_POINTER_HPP
#define XENIUM_HAZARD_POINTER_HPP

#include <xenium/reclamation/detail/allocation_tracker.hpp>
#include <xenium/reclamation/detail/concurrent_ptr.hpp>
#include <xenium/reclamation/detail/deletable_object.hpp>
#include <xenium/reclamation/detail/guard_ptr.hpp>
#include <xenium/reclamation/detail/thread_block_list.hpp>

#include <xenium/acquire_guard.hpp>
#include <xenium/parameter.hpp>
#include <xenium/policy.hpp>

#include <memory>
#include <stdexcept>

namespace xenium::reclamation {

/**
 * @brief This exception is thrown if a thread tries to allocate a new hazard pointer, but
 * the number of available hazard pointers is exhausted. This can only happen when
 * `xenium::reclamation::hp_allocation::static_strategy` is used.
 */
class bad_hazard_pointer_alloc : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

namespace detail {
  template <class Strategy, class Derived>
  struct basic_hp_thread_control_block;

  template <size_t K_, size_t A, size_t B, template <class> class ThreadControlBlock>
  struct generic_hp_allocation_strategy {
    static constexpr size_t K = K_;

    static size_t retired_nodes_threshold() { return A * number_of_active_hazard_pointers() + B; }

    static size_t number_of_active_hazard_pointers() { return number_of_active_hps.load(std::memory_order_relaxed); }

    using thread_control_block = ThreadControlBlock<generic_hp_allocation_strategy>;

  private:
    friend thread_control_block;
    friend basic_hp_thread_control_block<generic_hp_allocation_strategy, thread_control_block>;

    inline static std::atomic<size_t> number_of_active_hps{0};
  };

  template <class Strategy>
  struct static_hp_thread_control_block;

  template <class Strategy>
  struct dynamic_hp_thread_control_block;
} // namespace detail

namespace hp_allocation {
  /**
   * @brief Hazard pointer allocation strategy for a static number of hazard pointers per thread.
   *
   * The threshold for the number of retired nodes is calculated as `A * K * num_threads + B`;
   * `K`, `A` and `B` can be configured via template parameter.
   *
   * @tparam K The max. number of hazard pointers that can be allocated at the same time.
   * @tparam A
   * @tparam B
   */
  template <size_t K = 2, size_t A = 2, size_t B = 100>
  struct static_strategy : detail::generic_hp_allocation_strategy<K, A, B, detail::static_hp_thread_control_block> {};

  /**
   * @brief Hazard pointer allocation strategy for a dynamic number of hazard pointers per thread.
   *
   * This strategy uses a linked list of segments for the hazard pointers. The first segment can
   * hold `K` hazard pointers; subsequently allocated segments can hold 1.5x the number of hazard
   * pointers as the sum of all previous segments.
   *
   * The threshold for the number of retired nodes is calculated as `A * available_hps + B`, where
   * `available_hps` is the max. number of hazard pointers that could be allocated by all threads
   * at that time, without growing the number of segments. E.g., if `K = 2` and we have two threads
   * each with a single segment, then `available_hps = 4`; if one thread later allocates additional
   * hps and increases the number of segments such that they can hold up to 10 hazard pointers,
   * then `available_hps = 10+2 = 12`.
   *
   * `K`, `A` and `B` can be configured via template parameter.
   *
   * @tparam K The initial number of hazard pointers (i.e., the number of hps that can be allocated
   * without the need to grow).
   * @tparam A
   * @tparam B
   */
  template <size_t K = 2, size_t A = 2, size_t B = 100>
  struct dynamic_strategy : detail::generic_hp_allocation_strategy<K, A, B, detail::dynamic_hp_thread_control_block> {};
} // namespace hp_allocation

template <class AllocationStrategy = hp_allocation::static_strategy<3>>
struct hazard_pointer_traits {
  using allocation_strategy = AllocationStrategy;

  template <class... Policies>
  using with =
    hazard_pointer_traits<parameter::type_param_t<policy::allocation_strategy, AllocationStrategy, Policies...>>;
};

/**
 * @brief An implementation of the hazard pointers reclamation scheme as proposed by Michael
 * \[[Mic04](index.html#ref-michael-2004)\].
 *
 * For general information about the interface of the reclamation scheme see @ref reclamation_schemes.
 *
 * This class does not take a list of policies, but a `Traits` type that can be customized
 * with a list of policies. The following policies are supported:
 *  * `xenium::policy::allocation_strategy`<br>
 *    Defines how hazard pointers are allocated and how the threshold for the number of
 *    retired nodes is calculated.
 *    Possible arguments are `xenium::reclamation::hp_allocation::static_strategy`
 *    and 'xenium::reclamation::hp_allocation::dynamic_strategy`, where both strategies
 *    can be further customized via their respective template parameters.
 *    (defaults to `xenium::reclamation::he_allocation::static_strategy<3>`)
 *
 * @tparam Traits
 */
template <typename Traits = hazard_pointer_traits<>>
class hazard_pointer {
  using allocation_strategy = typename Traits::allocation_strategy;
  using thread_control_block = typename allocation_strategy::thread_control_block;
  friend detail::basic_hp_thread_control_block<allocation_strategy, thread_control_block>;

  template <class T, class MarkedPtr>
  class guard_ptr;

public:
  /**
   * @brief Customize the reclamation scheme with the given policies.
   *
   * The given policies are applied to the current configuration, replacing previously
   * specified policies of the same type.
   *
   * The resulting type is the newly configured reclamation scheme.
   *
   * @tparam Policies list of policies to customize the behaviour
   */
  template <class... Policies>
  using with = hazard_pointer<typename Traits::template with<Policies...>>;

  template <class T, std::size_t N = 0, class Deleter = std::default_delete<T>>
  class enable_concurrent_ptr;

  class region_guard {};

  template <class T, std::size_t N = T::number_of_mark_bits>
  using concurrent_ptr = detail::concurrent_ptr<T, N, guard_ptr>;

  ALLOCATION_TRACKER;

private:
  struct thread_data;

  inline static detail::thread_block_list<thread_control_block> global_thread_block_list;
  inline static thread_local thread_data local_thread_data;

  ALLOCATION_TRACKING_FUNCTIONS;
};

template <typename Traits>
template <class T, std::size_t N, class Deleter>
class hazard_pointer<Traits>::enable_concurrent_ptr :
    private detail::deletable_object_impl<T, Deleter>,
    private detail::tracked_object<hazard_pointer> {
public:
  static constexpr std::size_t number_of_mark_bits = N;

protected:
  enable_concurrent_ptr() noexcept = default;
  enable_concurrent_ptr(const enable_concurrent_ptr&) noexcept = default;
  enable_concurrent_ptr(enable_concurrent_ptr&&) noexcept = default;
  enable_concurrent_ptr& operator=(const enable_concurrent_ptr&) noexcept = default;
  enable_concurrent_ptr& operator=(enable_concurrent_ptr&&) noexcept = default;
  ~enable_concurrent_ptr() noexcept override = default;

private:
  friend detail::deletable_object_impl<T, Deleter>;

  template <class, class>
  friend class guard_ptr;
};

template <typename Traits>
template <class T, class MarkedPtr>
class hazard_pointer<Traits>::guard_ptr : public detail::guard_ptr<T, MarkedPtr, guard_ptr<T, MarkedPtr>> {
  using base = detail::guard_ptr<T, MarkedPtr, guard_ptr>;
  using Deleter = typename T::Deleter;

public:
  guard_ptr() noexcept = default;

  // Guard a marked ptr.
  explicit guard_ptr(const MarkedPtr& p);
  guard_ptr(const guard_ptr& p);
  guard_ptr(guard_ptr&& p) noexcept;

  guard_ptr& operator=(const guard_ptr& p);
  guard_ptr& operator=(guard_ptr&& p) noexcept;

  // Atomically take snapshot of p, and *if* it points to unreclaimed object, acquire shared ownership of it.
  void acquire(const concurrent_ptr<T>& p, std::memory_order order = std::memory_order_seq_cst);

  // Like acquire, but quit early if a snapshot != expected.
  bool acquire_if_equal(const concurrent_ptr<T>& p,
                        const MarkedPtr& expected,
                        std::memory_order order = std::memory_order_seq_cst);

  // Release ownership. Postcondition: get() == nullptr.
  void reset() noexcept;

  // Reset. Deleter d will be applied some time after all owners release their ownership.
  void reclaim(Deleter d = Deleter()) noexcept;

private:
  using enable_concurrent_ptr = hazard_pointer::enable_concurrent_ptr<T, MarkedPtr::number_of_mark_bits, Deleter>;

  friend base;
  void do_swap(guard_ptr& g) noexcept;

  typename thread_control_block::hazard_pointer* hp = nullptr;
};
} // namespace xenium::reclamation

#define HAZARD_POINTER_IMPL
#include <xenium/reclamation/impl/hazard_pointer.hpp>
#undef HAZARD_POINTER_IMPL

#endif
