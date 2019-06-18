
#pragma once

#include "config.hpp"

#include <boost/property_tree/ptree.hpp>

#define DYNAMIC_PARAM "<dynamic>"

template <class T>
struct descriptor;

// TODO - adapt for generic_epoch_based

#ifdef WITH_EPOCH_BASED
#include <xenium/reclamation/epoch_based.hpp>

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

#ifdef WITH_NEW_EPOCH_BASED
#include <xenium/reclamation/new_epoch_based.hpp>

template <size_t UpdateThreshold>
struct descriptor<xenium::reclamation::new_epoch_based<UpdateThreshold>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "new_epoch_based");
    pt.put("update_threshold", UpdateThreshold);
    return pt;
  }
};
#endif

#ifdef WITH_QUIESCENT_STATE_BASED
#include <xenium/reclamation/quiescent_state_based.hpp>

template <>
struct descriptor<xenium::reclamation::quiescent_state_based> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "quiescent_state_based");
    return pt;
  }
};
#endif

#ifdef WITH_DEBRA
#include <xenium/reclamation/debra.hpp>

template <size_t UpdateThreshold>
struct descriptor<xenium::reclamation::debra<UpdateThreshold>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "debra");
    pt.put("update_threshold", UpdateThreshold);
    return pt;
  }
};
#endif

#ifdef WITH_HAZARD_POINTER
#include <xenium/reclamation/hazard_pointer.hpp>

template <class Policy>
struct descriptor<xenium::reclamation::hazard_pointer<Policy>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "hazard_pointer");
    pt.put_child("policy", descriptor<Policy>::generate());
    return pt;
  }
};

template <size_t K, size_t A, size_t B>
struct descriptor<xenium::reclamation::dynamic_hazard_pointer_policy<K, A, B>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "dynamic_hazard_pointer_policy");
    pt.put("K", K);
    pt.put("A", A);
    pt.put("B", B);
    return pt;
  }
};

template <size_t K, size_t A, size_t B>
struct descriptor<xenium::reclamation::static_hazard_pointer_policy<K, A, B>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "static_hazard_pointer_policy");
    pt.put("K", K);
    pt.put("A", A);
    pt.put("B", B);
    return pt;
  }
};
#endif