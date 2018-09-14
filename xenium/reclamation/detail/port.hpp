#pragma once

#include <stdlib.h>
#include <boost/predef.h>

#if defined(BOOST_COMP_MSVC_DETECTION)
  #define SELECT_ANY __declspec(selectany)
#elif defined(BOOST_COMP_GNUC_DETECTION)
  #define SELECT_ANY __attribute__((weak))
#else
  #error "Unsupported compiler"
#endif
