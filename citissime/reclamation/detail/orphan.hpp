#ifndef CITISSIME_DETAIL_ORPHAN_HPP
#define CITISSIME_DETAIL_ORPHAN_HPP

#include "deletable_object.hpp"
#include <array>

namespace citissime { namespace reclamation { namespace detail
{

template <unsigned Epochs>
struct orphan : detail::deletable_object_impl<orphan<Epochs>>
{
  orphan(unsigned target_epoch, std::array<detail::deletable_object*, Epochs> &retire_lists):
    target_epoch(target_epoch),
    retire_lists(retire_lists)
  {}

  ~orphan()
  {
    for (auto p: retire_lists)
      detail::delete_objects(p);
  }

  const unsigned target_epoch;
private:
  std::array<detail::deletable_object*, Epochs> retire_lists;
};

}}}

#endif
