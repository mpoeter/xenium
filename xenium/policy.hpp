//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_POLICY_HPP
#define XENIUM_POLICY_HPP

#include <cstdint>

namespace xenium::policy {

/**
 * @brief Policy to configure the reclamation scheme to be used.
 *
 * This policy is used by the following data structures:
 *   * `michael_scott_queue`
 *   * `ramalhete_queue`
 *   * `harris_michael_list_based_set`
 *   * `harris_michael_hash_map`
 *
 * @tparam Reclaimer
 */
template <class Reclaimer>
struct reclaimer;

/**
 * @brief Policy to configure the backoff strategy.
 *
 * This policy is used by the following data structures:
 *   * `michael_scott_queue`
 *   * `ramalhete_queue`
 *   * `harris_michael_list_based_set`
 *   * `harris_michael_hash_map`
 *
 * @tparam Backoff
 */
template <class Backoff>
struct backoff;

/**
 * @brief Policy to configure the comparison function.
 *
 * This policy is used by the following data structures:
 *   * `harris_michael_list_based_set`
 *
 * @tparam Compare
 */
template <class Backoff>
struct compare;

/**
 * @brief Policy to configure the capacity of various containers.
 *
 * This policy is used by the following data structures:
 *   * `chase_work_stealing_deque`
 *
 * @tparam Value
 */
template <std::size_t Value>
struct capacity;

/**
 * @brief Policy to configure the internal container type of some
 *   data structures.
 *
 * This policy is used by the following data structures:
 *   * `chase_work_stealing_deque`
 *
 * @tparam Container
 */
template <class Container>
struct container;

/**
 * @brief Policy to configure the hash function.
 *
 * This policy is used by the following data structures:
 *   * `harris_michael_hash_map`
 *   * `vyukov_hash_map`
 *
 * @tparam T
 */
template <class T>
struct hash;

/**
 * @brief Policy to configure the allocation strategy.
 *
 * This policy is used by the following reclamation schemes:
 *   * `xenium::reclamation::hazard_pointer`
 *
 * @tparam T
 */
template <class T>
struct allocation_strategy;

/**
 * @brief Policy to configure the number of entries per allocated node in `ramalhete_queue`.
 * @tparam Value
 */
template <unsigned Value>
struct entries_per_node;

/**
 * @brief Policy to configure the number of padding bytes to add to each entry in
 * `kirsch_kfifo_queue` and `kirsch_bounded_kfifo_queue` to reduce false sharing.
 *
 * Note that this number of bytes is a lower bound. Depending on the size of the
 * queue's `value_type` the compiler may add some additional padding. The effective
 * size of a queue entry is provided in `entry_size`.
 *
 * @tparam Value
 */
template <unsigned Value>
struct padding_bytes;

/**
 * @brief Policy to configure the number of iterations to spin on a queue entry while waiting
 * for a pending push operation to finish.
 * @tparam Value
 */
template <unsigned Value>
struct pop_retries;
} // namespace xenium::policy
#endif
