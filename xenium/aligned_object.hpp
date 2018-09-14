#ifndef XENIUM_ALIGNED_OBJECT_HPP
#define XENIUM_ALIGNED_OBJECT_HPP

#include <boost/align/aligned_alloc.hpp>

namespace xenium {

  template <typename Derived, std::size_t Alignment = 0>
  struct aligned_object
  {
    static void* operator new(size_t sz)
    {
      return boost::alignment::aligned_alloc(
        Alignment == 0 ? std::alignment_of<Derived>() : Alignment, sz);
    }

    static void operator delete(void* p)
    {
      boost::alignment::aligned_free(p);
    }
  };
}

#endif
