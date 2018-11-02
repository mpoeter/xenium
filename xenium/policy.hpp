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
 * @tparam Value
 */
template <std::size_t Value>
struct slots_per_node;

/**
 * TODO
 * @tparam Backoff
 */
template <class Backoff>
struct backoff;

}}
#endif
