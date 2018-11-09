//
// Copyright (c) 2018 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_POLICY_HPP
#define XENIUM_POLICY_HPP

#include <cstdint>

namespace xenium { namespace policy {

/**
 * TODO
 * @tparam Reclaimer
 */
template <class Reclaimer>
struct reclaimer;

/**
 * TODO
 * @tparam Backoff
 */
template <class Backoff>
struct backoff;

}}
#endif
