#include "descriptor.hpp"

#ifdef WITH_GENERIC_EPOCH_BASED
#include <xenium/reclamation/generic_epoch_based.hpp>

inline std::string to_string(xenium::reclamation::region_extension v) {
  switch (v) {
    case xenium::reclamation::region_extension::eager: return "eager";
    case xenium::reclamation::region_extension::lazy: return "lazy";
    case xenium::reclamation::region_extension::none: return "none";
    default: assert(false); return "<invalid region_extension>";
  }
}

template <>
struct descriptor<xenium::reclamation::scan::all_threads> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "all_threads");
    return pt;
  }
};

template <>
struct descriptor<xenium::reclamation::scan::one_thread> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "one_thread");
    return pt;
  }
};

template <unsigned N>
struct descriptor<xenium::reclamation::scan::n_threads<N>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "n_threads");
    pt.put("n", N);
    return pt;
  }
};

template <>
struct descriptor<xenium::reclamation::abandon::never> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "never");
    return pt;
  }
};

template <>
struct descriptor<xenium::reclamation::abandon::always> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "always");
    return pt;
  }
};

template <size_t Threshold>
struct descriptor<xenium::reclamation::abandon::when_exceeds_threshold<Threshold>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "when_exceeds_threshold");
    pt.put("threshold", Threshold);
    return pt;
  }
};

template <class Traits>
struct descriptor<xenium::reclamation::generic_epoch_based<Traits>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "generic_epoch_based");
    // for some reason GCC does not like it if `Traits::scan_frequency` is passed directly...
    constexpr auto scan_frequency = Traits::scan_frequency;
    pt.put("scan_frequency", scan_frequency);
    pt.put_child("scan_strategy", descriptor<typename Traits::scan_strategy>::generate());
    pt.put_child("abandon_strategy", descriptor<typename Traits::abandon_strategy>::generate());
    pt.put("region_extension", to_string(Traits::region_extension_type));
    return pt;
  }
};
#endif

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

template <class Traits>
struct descriptor<xenium::reclamation::hazard_pointer<Traits>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "hazard_pointer");
    pt.put_child("allocation_strategy", descriptor<typename Traits::allocation_strategy>::generate());
    return pt;
  }
};

template <size_t K, size_t A, size_t B>
struct descriptor<xenium::reclamation::hp_allocation::dynamic_strategy<K, A, B>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "dynamic");
    pt.put("K", K);
    pt.put("A", A);
    pt.put("B", B);
    return pt;
  }
};

template <size_t K, size_t A, size_t B>
struct descriptor<xenium::reclamation::hp_allocation::static_strategy<K, A, B>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "static");
    pt.put("K", K);
    pt.put("A", A);
    pt.put("B", B);
    return pt;
  }
};
#endif

#ifdef WITH_VYUKOV_HASH_MAP
#include <xenium/vyukov_hash_map.hpp>
template <class Key, class Value, class... Policies>
struct descriptor<xenium::vyukov_hash_map<Key, Value, Policies...>> {
  static boost::property_tree::ptree generate() {
    using hash_map = xenium::vyukov_hash_map<Key, Value, Policies...>;
    boost::property_tree::ptree pt;
    pt.put("type", "vyukov_hash_map");
    pt.put("initial_capacity", DYNAMIC_PARAM);
    pt.put_child("reclaimer", descriptor<typename hash_map::reclaimer>::generate());
    return pt;
  }
};
#endif