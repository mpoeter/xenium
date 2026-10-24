//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_HAZARD_ERAS_HPP
#define XENIUM_HAZARD_ERAS_HPP

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
 * @brief This exception is thrown if a thread tries to allocate a new hazard era, but
 * the number of available hazard eras is exhausted. This can only happen when
 * `xenium::reclamation::he_allocation::static_strategy` is used.
 */
class bad_hazard_era_alloc : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

namespace detail {
  struct deletable_object_with_eras;

  template <class Strategy, class Derived>
  struct basic_he_thread_control_block;

  template <size_t K_, size_t A, size_t B, template <class> class ThreadControlBlock>
  struct generic_hazard_era_allocation_strategy {
    static constexpr size_t K = K_;

    static size_t retired_nodes_threshold() { return A * number_of_active_hazard_eras() + B; }

    static size_t number_of_active_hazard_eras() { return number_of_active_hes.load(std::memory_order_relaxed); }

    using thread_control_block = ThreadControlBlock<generic_hazard_era_allocation_strategy>;

  private:
    friend thread_control_block;
    friend basic_he_thread_control_block<generic_hazard_era_allocation_strategy, thread_control_block>;

    inline static std::atomic<size_t> number_of_active_hes{0};
  };

  template <class Strategy>
  struct static_he_thread_control_block;

  template <class Strategy>
  struct dynamic_he_thread_control_block;
} // namespace detail

namespace he_allocation {
  /**
   * @brief Hazard era allocation strategy for a static number of hazard eras per thread.
   *
   * The threshold for the number of retired nodes is calculated as `A * K * num_threads + B`;
   * `K`, `A` and `B` can be configured via template parameter.
   *
   * Note that in contrast to the `hazard_pointer` reclamation scheme, a `hazard_eras::guard_ptr`
   * may use the same hazard era entry as a previously created `guard_ptr` if they observe the
   * same era. However, since this is not guarenteed, one has to use the same upper bound for the
   * number of hazard eras as when using the `hazard_pointer` scheme.
   *
   * @tparam K The max. number of hazard eras that can be allocated at the same time.
   * @tparam A
   * @tparam B
   */
  template <size_t K = 2, size_t A = 2, size_t B = 100>
  struct static_strategy :
      detail::generic_hazard_era_allocation_strategy<K, A, B, detail::static_he_thread_control_block> {};

  /**
   * @brief Hazard era allocation strategy for a dynamic number of hazard eras per thread.
   *
   * This strategy uses a linked list of segments for the hazard eras. The first segment can
   * hold `K` hazard eras; subsequently allocated segments can hold 1.5x the number of hazard
   * eras as the sum of all previous segments.
   *
   * The threshold for the number of retired nodes is calculated as `A * available_hes + B`, where
   * `available_hes` is the max. number of hazard eras that could be allocated by all threads
   * at that time, without growing the number of segments. E.g., if `K = 2` and we have two threads
   * each with a single segment, then `available_hes = 4`; if one thread later allocates additional
   * hes and increases the number of segments such that they can hold up to 10 hazard eras,
   * then `available_hes = 10+2 = 12`.
   *
   * `K`, `A` and `B` can be configured via template parameter.
   *
   * @tparam K The initial number of hazard eras (i.e., the number of hes that can be allocated
   * without the need to grow).
   * @tparam A
   * @tparam B
   */
  template <size_t K = 2, size_t A = 2, size_t B = 100>
  struct dynamic_strategy :
      detail::generic_hazard_era_allocation_strategy<K, A, B, detail::dynamic_he_thread_control_block> {};
} // namespace he_allocation

template <class AllocationStrategy = he_allocation::static_strategy<3>>
struct hazard_era_traits {
  using allocation_strategy = AllocationStrategy;

  template <class... Policies>
  using with = hazard_era_traits<parameter::type_param_t<policy::allocation_strategy, AllocationStrategy, Policies...>>;
};

/**
 * @brief An implementation of the hazard eras scheme proposed by Ramalhete and Correia
 * \[[RC17](index.html#ref-ramalhete-2017)\].
 *
 * For general information about the interface of the reclamation scheme see @ref reclamation_schemes.
 *
 * This class does not take a list of policies, but a `Traits` type that can be customized
 * with a list of policies. The following policies are supported:
 *  * `xenium::policy::allocation_strategy`<br>
 *    Defines how hazard eras are allocated and how the threshold for the number of
 *    retired nodes is calculated.
 *    Possible arguments are `xenium::reclamation::he_allocation::static_strategy`
 *    and 'xenium::reclamation::he_allocation::dynamic_strategy`, where both strategies
 *    can be further customized via their respective template parameters.
 *    (defaults to `xenium::reclamation::he_allocation::static_strategy<3>`)
 *
 * @tparam Traits
 */
template <class Traits = hazard_era_traits<>>
class hazard_eras {
  using allocation_strategy = typename Traits::allocation_strategy;
  using thread_control_block = typename allocation_strategy::thread_control_block;
  friend detail::basic_he_thread_control_block<allocation_strategy, thread_control_block>;

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
  using with = hazard_eras<typename Traits::template with<Policies...>>;

  template <class T, std::size_t N = 0, class Deleter = std::default_delete<T>>
  class enable_concurrent_ptr;

  class region_guard {};

  template <class T, std::size_t N = T::number_of_mark_bits>
  using concurrent_ptr = detail::concurrent_ptr<T, N, guard_ptr>;

  ALLOCATION_TRACKER;

private:
  struct thread_data;

  friend struct detail::deletable_object_with_eras;

  using era_t = uint64_t;
  inline static std::atomic<era_t> era_clock{1};
  inline static detail::thread_block_list<thread_control_block, detail::deletable_object_with_eras>
    global_thread_block_list{};
  static thread_data& local_thread_data();

  ALLOCATION_TRACKING_FUNCTIONS;
};

template <class Traits>
template <class T, std::size_t N, class Deleter>
class hazard_eras<Traits>::enable_concurrent_ptr :
    public detail::deletable_object_impl<T, Deleter, detail::deletable_object_with_eras>,
    private detail::tracked_object<hazard_eras> {
public:
  static constexpr std::size_t number_of_mark_bits = N;

protected:
  enable_concurrent_ptr() noexcept { this->construction_era = era_clock.load(std::memory_order_relaxed); }
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

template <class Traits>
template <class T, class MarkedPtr>
class hazard_eras<Traits>::guard_ptr : public detail::guard_ptr<T, MarkedPtr, guard_ptr<T, MarkedPtr>> {
  using base = detail::guard_ptr<T, MarkedPtr, guard_ptr>;
  using Deleter = typename T::Deleter;

public:
  guard_ptr() noexcept = default;

  // Guard a marked ptr.
  explicit guard_ptr(const MarkedPtr& p);
  guard_ptr(const guard_ptr& p);
  guard_ptr(guard_ptr&& p) noexcept;

  guard_ptr& operator=(const guard_ptr& p) noexcept;
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
  using enable_concurrent_ptr = hazard_eras::enable_concurrent_ptr<T, MarkedPtr::number_of_mark_bits, Deleter>;

  friend base;
  void do_swap(guard_ptr& g) noexcept;

  typename thread_control_block::hazard_era* he = nullptr;
};

namespace detail {
  struct deletable_object_with_eras {
    virtual void delete_self() = 0;
    deletable_object_with_eras* next = nullptr;

  protected:
    virtual ~deletable_object_with_eras() = default;
    using era_t = size_t;
    era_t construction_era{};
    era_t retirement_era{};
    template <class>
    friend class hazard_eras;

#ifdef __clang__
  #pragma clang diagnostic push
  // clang does not support dependent nested types for friend class declaration
  #pragma clang diagnostic ignored "-Wunsupported-friend"
#endif
    template <class T>
    friend struct hazard_eras<T>::thread_data;
#ifdef __clang__
  #pragma clang diagnostic pop
#endif
  };
} // namespace detail
} // namespace xenium::reclamation

#define HAZARD_ERAS_IMPL
#include <xenium/reclamation/impl/hazard_eras.hpp>
#undef HAZARD_ERAS_IMPL

#endif
