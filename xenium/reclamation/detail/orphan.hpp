//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_DETAIL_ORPHAN_HPP
#define XENIUM_DETAIL_ORPHAN_HPP

#include <array>
#include <xenium/reclamation/detail/deletable_object.hpp>

namespace xenium::reclamation::detail {

template <unsigned Epochs>
struct orphan : detail::deletable_object_impl<orphan<Epochs>> {
  orphan(unsigned target_epoch, std::array<detail::deletable_object*, Epochs>& retire_lists) :
      target_epoch(target_epoch),
      retire_lists(retire_lists) {}

  ~orphan() override {
    for (auto p : retire_lists) {
      detail::delete_objects(p);
    }
  }

  const unsigned target_epoch;

private:
  std::array<detail::deletable_object*, Epochs> retire_lists;
};

} // namespace xenium::reclamation::detail

#endif
