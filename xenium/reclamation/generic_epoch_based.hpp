//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_GENERIC_EPOCH_BASED_HPP
#define XENIUM_GENERIC_EPOCH_BASED_HPP

#include <xenium/acquire_guard.hpp>
#include <xenium/parameter.hpp>
#include <xenium/reclamation/detail/allocation_tracker.hpp>
#include <xenium/reclamation/detail/concurrent_ptr.hpp>
#include <xenium/reclamation/detail/deletable_object.hpp>
#include <xenium/reclamation/detail/guard_ptr.hpp>
#include <xenium/reclamation/detail/retire_list.hpp>
#include <xenium/reclamation/detail/thread_block_list.hpp>

#include <algorithm>

namespace xenium {

namespace reclamation {
  /**
   * @brief This namespace contains various scan strategies to be used
   * in the `generic_epoch_based` reclamation scheme.
   *
   * These strategies define how many threads should be scanned in an
   * attempt to update the global epoch.
   */
  namespace scan {
    struct all_threads;
    struct one_thread;
    template <unsigned N>
    struct n_threads;
  } // namespace scan

  /**
   * @brief This namespace contains various abandonment strategies to be used
   * in the `generic_epoch_based` reclamation scheme.
   *
   * These strategies define when a thread should abandon its retired nodes
   * (i.e., put them on the global list of retired nodes. A thread will always
   * abandon any remaining retired nodes when it terminates.
   *
   * @note The abandon strategy is applied for the retire list of each epoch
   * individually. This is important in case the `when_exceeds_threshold` policy
   * is used.
   */
  namespace abandon {
    struct never;
    struct always;
    template <size_t Threshold>
    struct when_exceeds_threshold;
  } // namespace abandon

  /**
   * @brief Defines whether the size of a critical region can be extended using `region_guard`s.
   */
  enum class region_extension {
    /**
     * @brief Critical regions are never extended, i.e., `region_guards` are effectively
     * no-ops.
     */
    none,

    /**
     * @brief The critical region is entered upon construction of the `region_guard` and
     * left once the `region_guard` gets destroyed.
     */
    eager,

    /**
     * @brief A critical region is only entered when a `guard_ptr` is created. But if this
     * is done inside the scope of a region_guard, the critical region is only left once
     * the `region_guard` gets destroyed.
     */
    lazy
  };
} // namespace reclamation

namespace policy {
  /**
   * @brief Policy to configure the scan frequency for `generic_epoch_based` reclamation.
   *
   * This policy is used to define how often a thread should scan the epochs of the other
   * threads in an attempt to update the global epoch. The value is based on the number of
   * _critical region entries_; a scan is performed on every n'th critical region entry,
   * since the thread last observed a new epoch, i.e., a higher number means scans are
   * performed less frequently.
   * A critical region entry corresponds to the creation of a stand-alone non-empty `guard_ptr`.
   * Creation of other `guard_ptr`s while the thread is inside a critical region (i.e., some
   * other non-empty `guard_ptr` already exists), do not count as critical region entries.
   *
   * @tparam Value
   */
  template <std::size_t Value>
  struct scan_frequency;

  /**
   * @brief Policy to configure the scan strategy for `generic_epoch_based` reclamation.
   *
   * This policy is used to define how many threads should be scanned in an attempt to update
   * the global epoch. Possible values are:
   *   * `xenium::reclamation::scan::all_threads` - all threads are scanned each time.
   *   * `xenium::reclamation::scan::one_thread` - only a single thread is scanned;
   *   * `xenium::reclamation::scan::n_threads` - scans _n_ threads, where _n_ is a constant
   *     that can be define via a template parameter.
   *
   * This policy is closely related to `xenium::policy::scan_frequency`; the `scan_frequency`
   * defines how often the scan strategy shall be applied. If only some threads are scanned
   * (i.e., `one_thread` or `n_threads` is used), scans should happen more frequently, so a
   * smaller `scan_frequency` should be chosen than when using `all_threads`.
   *
   * @tparam T
   */

  template <class T>
  struct scan;

  /**
   * @brief Policy to configure the abandon strategy for `generic_epoch_based` reclamation.
   *
   * This policy defines if/when a thread should abandon its local retired nodes when leaving the
   * critical region. Every thread gathers its retired nodes in thread-local lists and delays
   * reclamation until a later time when it is guaranteed that no thread holds a `guard_ptr` to any
   * of them. A thread may abandon its retired nodes by moving them to a global list. Any thread
   * that performs reclamation of local retired nodes tries to adopt the list of abandoned retired
   * nodes and reclaim those for which it is safe.
   *
   * Possible values are:
   *   * `xenium::reclamation::abandon::never` - never abandon local retired nodes (except when
   *     the thread terminates).
   *   * `xenium::reclamation::abandon::always` - always abandon local retired nodes when leaving
   *      the critical region.
   *   * `xenium::reclamation::abandon::when_exceeds_threshold` - abandon the local retired nodes
   *     if the number of remaining nodes exceeds the defined threshold at the time the thread
   *     leaves the critical region.
   *
   * @tparam T
   */
  template <class T>
  struct abandon;

  /**
   * @brief Policy to configure the extension of critical regions in `generic_epoch_based` reclamation.
   *
   * This policy defines the effect of a `region_guard` instance. By default, a thread enters a
   * critical region upon acquiring a `guard_ptr` to some node. This involves a sequential consistent
   * fence which is relatively expensive, so it might make sense to extend the size of the region and
   * only pay the price of the fence once. This can be achieved by using `region_guard`s. The
   * `region_extension` policy allows to define if/how a `region_guard` extends the size of a critical
   * region. Possible values are:
   *   * `xenium::reclamation::region_extension::none` - no region extension is applied; construction
   *     and destruction of `region_guard` instances are no-ops.
   *   * `xenium::reclamation::region_extension::eager` - the size of the critical region is extended
   *     to the scope of the `region_guard` instance, i.e., the thread enters the critical region upon
   *     creation of the `region_guard` instance, and leaves the critical region when the `region_guard`
   *     gets destroyed.
   *   * `xenium::reclamation::region_extension::lazy` - the region is only entered whe a `guard_ptr`
   *     is acquired. However, once the the critical region has been entered it is only left once the
   *     `region_guard` is destroyed.
   *
   * @tparam Value
   */
  template <reclamation::region_extension Value>
  struct region_extension;
} // namespace policy

namespace reclamation {
  template <std::size_t ScanFrequency = 100,
            class ScanStrategy = scan::all_threads,
            class AbandonStrategy = abandon::never,
            region_extension RegionExtension = region_extension::eager>
  struct generic_epoch_based_traits {
    static constexpr std::size_t scan_frequency = ScanFrequency;
    using scan_strategy = ScanStrategy;
    using abandon_strategy = AbandonStrategy;
    static constexpr region_extension region_extension_type = RegionExtension;

    template <class... Policies>
    using with = generic_epoch_based_traits<
      parameter::value_param_t<std::size_t, policy::scan_frequency, ScanFrequency, Policies...>::value,
      parameter::type_param_t<policy::scan, ScanStrategy, Policies...>,
      parameter::type_param_t<policy::abandon, AbandonStrategy, Policies...>,
      parameter::value_param_t<region_extension, policy::region_extension, RegionExtension, Policies...>::value>;
  };

  /**
   * @brief A generalized implementation of epoch based reclamation.
   *
   * For general information about the interface of the reclamation scheme see @ref reclamation_schemes.
   *
   * This implementation is parameterized and can be configured in many ways. For example:
   * * like the original proposal for epoch based reclamation (EBR) by Fraser
   *   \[[Fra04](index.html#ref-fraser-2004)\]:
   * ```cpp
   * using ebr = generic_epoch_based<>::with<
   *   policy::scan<scan::all_threads>,
   *   policy::region_extension<region_extension::none>
   * >
   * ```
   * * like new epoch based reclamation (NEBR) as proposed by Hart et al.
   *   \[[HMBW07](index.html#ref-hart-2007)\]:
   * ```cpp
   * using nebr = generic_epoch_based<>::with<
   *   policy::scan<scan::all_threads>,
   *   policy::region_extension<region_extension::eager>
   * >
   * ```
   * * like DEBRA as proposed by Brown \[[Bro15](index.html#ref-brown-2015)\]:
   * ```cpp
   * using debra = generic_epoch_based<>::with<
   *   policy::scan<scan::one_thread>,
   *   policy::region_extension<region_extension::none>
   * >
   * ```
   * For ease of use these aliases are already predefined.
   *
   * This class does not take a list of policies, but a `Traits` type that can be customized
   * with a list of policies. Use the `with<>` template alias to pass your custom policies
   * (see the previous examples for `epoch_based`, `new_epoch_based` and `debra`).
   *
   * The following policies are supported:
   *  * `xenium::policy::scan_frequency`<br>
   *    Defines how often a thread should scan the epochs of the other threads in an attempt
   *    to update the global epoch. (defaults to 100)
   *  * `xenium::policy::scan`<br>
   *    Defines how many threads should be scanned. Possible values are `all_threads`,
   *    `one_thread` and `n_threads`. (defaults to `all_threads`)
   *  * `xenium::policy::abandon`<br>
   *    Defines when local retired nodes should be abandoned, so they can be reclaimed by some
   *    other thread. Possible values are `never`, `always` and `when_exceeds_threshold`.
   *    (defaults to `never`)
   *  * `xenium::policy::region_extension`<br>
   *    Defines the effect a `region_guard` should have. (defaults to `region_extension::eager`)
   *
   * @tparam Traits
   */
  template <class Traits = generic_epoch_based_traits<>>
  class generic_epoch_based {
    template <class T, class MarkedPtr>
    class guard_ptr;

    template <unsigned N>
    friend struct scan::n_threads;
    friend struct scan::all_threads;

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
    using with = generic_epoch_based<typename Traits::template with<Policies...>>;

    template <class T, std::size_t N = 0, class Deleter = std::default_delete<T>>
    class enable_concurrent_ptr;

    struct region_guard {
      region_guard() noexcept;
      ~region_guard() noexcept;

      region_guard(const region_guard&) = delete;
      region_guard(region_guard&&) = delete;
      region_guard& operator=(const region_guard&) = delete;
      region_guard& operator=(region_guard&&) = delete;
    };

    template <class T, std::size_t N = T::number_of_mark_bits>
    using concurrent_ptr = xenium::reclamation::detail::concurrent_ptr<T, N, guard_ptr>;

    ALLOCATION_TRACKER;

  private:
    using epoch_t = size_t;

    static constexpr epoch_t number_epochs = 3;

    struct thread_data;
    struct thread_control_block;

    inline static std::atomic<epoch_t> global_epoch;
    inline static detail::thread_block_list<thread_control_block> global_thread_block_list;
    inline static std::array<detail::orphan_list<>, number_epochs> orphans;
    inline static thread_local thread_data local_thread_data;

    ALLOCATION_TRACKING_FUNCTIONS;
  };

  template <class Traits>
  template <class T, std::size_t N, class Deleter>
  class generic_epoch_based<Traits>::enable_concurrent_ptr :
      private detail::deletable_object_impl<T, Deleter>,
      private detail::tracked_object<generic_epoch_based> {
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

  template <class Traits>
  template <class T, class MarkedPtr>
  class generic_epoch_based<Traits>::guard_ptr : public detail::guard_ptr<T, MarkedPtr, guard_ptr<T, MarkedPtr>> {
    using base = detail::guard_ptr<T, MarkedPtr, guard_ptr>;
    using Deleter = typename T::Deleter;

  public:
    // Guard a marked ptr.
    explicit guard_ptr(const MarkedPtr& p = MarkedPtr()) noexcept;
    guard_ptr(const guard_ptr& p) noexcept;
    guard_ptr(guard_ptr&& p) noexcept;

    guard_ptr& operator=(const guard_ptr& p) noexcept;
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

#define GENERIC_EPOCH_BASED_IMPL
#include "impl/generic_epoch_based.hpp"
#undef GENERIC_EPOCH_BASED_IMPL

namespace xenium::reclamation {
template <class... Policies>
using epoch_based = generic_epoch_based<>::with<policy::scan_frequency<100>,
                                                policy::scan<scan::all_threads>,
                                                policy::region_extension<region_extension::none>,
                                                Policies...>;

template <class... Policies>
using new_epoch_based = generic_epoch_based<>::with<policy::scan_frequency<100>,
                                                    policy::scan<scan::all_threads>,
                                                    policy::region_extension<region_extension::eager>,
                                                    Policies...>;

template <class... Policies>
using debra = generic_epoch_based<>::with<policy::scan_frequency<20>,
                                          policy::scan<scan::one_thread>,
                                          policy::region_extension<region_extension::none>,
                                          Policies...>;
} // namespace xenium::reclamation

#endif