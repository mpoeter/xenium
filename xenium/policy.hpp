//
// Copyright (c) 2018 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_POLICY_HPP
#define XENIUM_POLICY_HPP

#include <cstdint>

namespace xenium { namespace policy {

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

}}
#endif
