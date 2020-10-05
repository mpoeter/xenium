#include "descriptor.hpp"

#ifdef WITH_GENERIC_EPOCH_BASED
  #include <xenium/reclamation/generic_epoch_based.hpp>

  #include <tao/config/value.hpp>

inline std::string to_string(xenium::reclamation::region_extension v) {
  switch (v) {
    case xenium::reclamation::region_extension::eager:
      return "eager";
    case xenium::reclamation::region_extension::lazy:
      return "lazy";
    case xenium::reclamation::region_extension::none:
      return "none";
    default:
      assert(false);
      return "<invalid region_extension>";
  }
}

template <>
struct descriptor<xenium::reclamation::scan::all_threads> {
  static tao::json::value generate() { return {{"type", "all_threads"}}; }
};

template <>
struct descriptor<xenium::reclamation::scan::one_thread> {
  static tao::json::value generate() { return {{"type", "one_thread"}}; }
};

template <unsigned N>
struct descriptor<xenium::reclamation::scan::n_threads<N>> {
  static tao::json::value generate() { return {{"type", "n_threads"}, {"n", N}}; }
};

template <>
struct descriptor<xenium::reclamation::abandon::never> {
  static tao::json::value generate() { return {{"type", "never"}}; }
};

template <>
struct descriptor<xenium::reclamation::abandon::always> {
  static tao::json::value generate() { return {{"type", "always"}}; }
};

template <size_t Threshold>
struct descriptor<xenium::reclamation::abandon::when_exceeds_threshold<Threshold>> {
  static tao::json::value generate() { return {{"type", "when_exceeds_threshold"}, {"threshold", Threshold}}; }
};

template <class Traits>
struct descriptor<xenium::reclamation::generic_epoch_based<Traits>> {
  static tao::json::value generate() {
    return {{"type", "generic_epoch_based"},
            {"scan_frequency", Traits::scan_frequency},
            {"scan_strategy", descriptor<typename Traits::scan_strategy>::generate()},
            {"abandon_strategy", descriptor<typename Traits::abandon_strategy>::generate()},
            {"region_extension", to_string(Traits::region_extension_type)}};
  }
};
#endif

#ifdef WITH_QUIESCENT_STATE_BASED
  #include <xenium/reclamation/quiescent_state_based.hpp>

template <>
struct descriptor<xenium::reclamation::quiescent_state_based> {
  static tao::json::value generate() { return {{"type", "quiescent_state_based"}}; }
};
#endif

#ifdef WITH_HAZARD_POINTER
  #include <xenium/reclamation/hazard_pointer.hpp>

template <class Traits>
struct descriptor<xenium::reclamation::hazard_pointer<Traits>> {
  static tao::json::value generate() {
    return {{"type", "hazard_pointer"},
            {"allocation_strategy", descriptor<typename Traits::allocation_strategy>::generate()}};
  }
};

template <size_t K, size_t A, size_t B>
struct descriptor<xenium::reclamation::hp_allocation::dynamic_strategy<K, A, B>> {
  static tao::json::value generate() { return {{"type", "dynamic"}, {"K", K}, {"A", A}, {"B", B}}; }
};

template <size_t K, size_t A, size_t B>
struct descriptor<xenium::reclamation::hp_allocation::static_strategy<K, A, B>> {
  static tao::json::value generate() {
    return {
      {"type", "static"},
      {"A", A},
      {"K", K},
      {"B", B},
    };
  }
};
#endif
