
#pragma once

#include "config.hpp"

#include <boost/property_tree/ptree.hpp>

#ifdef WITH_EPOCH_BASED
#include <xenium/reclamation/epoch_based.hpp>
#endif

#ifdef WITH_QUIESCENT_STATE_BASED
#include <xenium/reclamation/quiescent_state_based.hpp>
#endif

template <class T>
struct descriptor;

#ifdef WITH_EPOCH_BASED
template <size_t UpdateThreshold>
struct descriptor<xenium::reclamation::epoch_based<UpdateThreshold>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "epoch_based");
    pt.put("update_threshold", UpdateThreshold);
    return pt;
  }
};
#endif

#ifdef WITH_QUIESCENT_STATE_BASED
template <>
struct descriptor<xenium::reclamation::quiescent_state_based> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "quiescent_state_based");
    return pt;
  }
};
#endif
