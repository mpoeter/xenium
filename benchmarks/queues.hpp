#include "benchmark.hpp"
#include "descriptor.hpp"
#include "reclaimers.hpp"

template <class T>
struct queue_builder {
  static auto create(const boost::property_tree::ptree&) { return std::make_unique<T>(); }
};

#ifdef WITH_RAMALHETE_QUEUE
#include <xenium/ramalhete_queue.hpp>

template <class T, class... Policies>
struct descriptor<xenium::ramalhete_queue<T, Policies...>> {
  static boost::property_tree::ptree generate() {
    using queue = xenium::ramalhete_queue<T, Policies...>;
    boost::property_tree::ptree pt;
    pt.put("type", "ramalhete_queue");
    pt.put_child("reclaimer", descriptor<typename queue::reclaimer>::generate());
    return pt;
  }
};

namespace {
  template <class T, class... Policies>
  bool try_push(xenium::ramalhete_queue<T*, Policies...>& queue, T item) {
    queue.push(new T(item));
    return true;
  }

  template <class T, class... Policies>
  bool try_pop(xenium::ramalhete_queue<T*, Policies...>& queue, T& item) {
    T* value;
    auto result = queue.try_pop(value);
    if (result) {
      item = *value;
      delete value;
    }
    return result;
  }
}
#endif

#ifdef WITH_MICHAEL_SCOTT_QUEUE
#include <xenium/michael_scott_queue.hpp>

template <class T, class... Policies>
struct descriptor<xenium::michael_scott_queue<T, Policies...>> {
  static boost::property_tree::ptree generate() {
    using queue = xenium::michael_scott_queue<T, Policies...>;
    boost::property_tree::ptree pt;
    pt.put("type", "michael_scott_queue");
    pt.put_child("reclaimer", descriptor<typename queue::reclaimer>::generate());
    return pt;
  }
};

namespace {
  template <class T, class... Policies>
  bool try_push(xenium::michael_scott_queue<T, Policies...>& queue, T item) {
    queue.push(std::move(item));
    return true;
  }

  template <class T, class... Policies>
  bool try_pop(xenium::michael_scott_queue<T, Policies...>& queue, T& item) {
    return queue.try_pop(item);
  }
}
#endif

#ifdef WITH_VYUKOV_BOUNDED_QUEUE
#include <xenium/vyukov_bounded_queue.hpp>

template <class T, class... Policies>
struct descriptor<xenium::vyukov_bounded_queue<T, Policies...>> {
  static boost::property_tree::ptree generate() {
    using queue = xenium::vyukov_bounded_queue<T, Policies...>;
    boost::property_tree::ptree pt;
    pt.put("type", "vyukov_bounded_queue");
    // for some reason GCC does not like it if `queue::default_to_weak` is passed directly...
    constexpr bool weak = queue::default_to_weak;
    pt.put("weak", weak);
    pt.put("size", DYNAMIC_PARAM);
    return pt;
  }
};

template <class T, class... Policies>
struct queue_builder<xenium::vyukov_bounded_queue<T, Policies...>> {
  static auto create(const boost::property_tree::ptree& config) {
    auto size = config.get<size_t>("size");
    if (!xenium::utils::is_power_of_two(size))
      throw std::runtime_error("vyukov_bounded_queue size must be a power of two");
    return std::make_unique<xenium::vyukov_bounded_queue<T, Policies...>>(size);
  }
};

template <class T, class... Policies>
struct region_guard<xenium::vyukov_bounded_queue<T, Policies...>> {
  // vyukov_bounded_queue does not have a reclaimer, so we define an
  // empty dummy type as region_guard placeholder.
  struct type{};
};

namespace {
  template <class T, class... Policies>
  bool try_push(xenium::vyukov_bounded_queue<T, Policies...>& queue, T item) {
    return queue.try_push(std::move(item));
  }

  template <class T, class... Policies>
  bool try_pop(xenium::vyukov_bounded_queue<T, Policies...>& queue, T& item) {
    return queue.try_pop(item);
  }
}
#endif

#ifdef WITH_KIRSCH_KFIFO_QUEUE
#include <xenium/kirsch_kfifo_queue.hpp>

template <class T, class... Policies>
struct descriptor<xenium::kirsch_kfifo_queue<T, Policies...>> {
  static boost::property_tree::ptree generate() {
    using queue = xenium::kirsch_kfifo_queue<T, Policies...>;
    boost::property_tree::ptree pt;
    pt.put("type", "kirsch_kfifo_queue");
    pt.put("k", DYNAMIC_PARAM);
    pt.put_child("reclaimer", descriptor<typename queue::reclaimer>::generate());
    return pt;
  }
};

template <class T, class... Policies>
struct queue_builder<xenium::kirsch_kfifo_queue<T, Policies...>> {
  static auto create(const boost::property_tree::ptree& config) {
    auto k = config.get<size_t>("k");
    return std::make_unique<xenium::kirsch_kfifo_queue<T, Policies...>>(k);
  }
};

namespace {
  template <class T, class... Policies>
  bool try_push(xenium::kirsch_kfifo_queue<T*, Policies...>& queue, T item) {
    queue.push(new T(item));
    return true;
  }

  template <class T, class... Policies>
  bool try_pop(xenium::kirsch_kfifo_queue<T*, Policies...>& queue, T& item) {
    T* value;
    auto result = queue.try_pop(value);
    if (result) {
      item = *value;
      delete value;
    }
    return result;
  }
}
#endif

#ifdef WITH_KIRSCH_BOUNDED_KFIFO_QUEUE
#include <xenium/kirsch_bounded_kfifo_queue.hpp>

template <class T, class... Policies>
struct descriptor<xenium::kirsch_bounded_kfifo_queue<T, Policies...>> {
  static boost::property_tree::ptree generate() {
    // TODO - consider padding parameter
    // using queue = xenium::kirsch_bounded_kfifo_queue<T, Policies...>;
    boost::property_tree::ptree pt;
    pt.put("type", "kirsch_bounded_kfifo_queue");
    pt.put("k", DYNAMIC_PARAM);
    pt.put("segments", DYNAMIC_PARAM);
    return pt;
  }
};

template <class T, class... Policies>
struct queue_builder<xenium::kirsch_bounded_kfifo_queue<T, Policies...>> {
  static auto create(const boost::property_tree::ptree& config) {
    auto k = config.get<size_t>("k");
    auto segments = config.get<size_t>("segments");
    return std::make_unique<xenium::kirsch_bounded_kfifo_queue<T, Policies...>>(k, segments);
  }
};

template <class T, class... Policies>
struct region_guard<xenium::kirsch_bounded_kfifo_queue<T, Policies...>> {
  // kirsch_bounded_kfifo_queue does not have a reclaimer, so we define an
  // empty dummy type as region_guard placeholder.
  struct type{};
};

namespace {
  template <class T, class... Policies>
  bool try_push(xenium::kirsch_bounded_kfifo_queue<T*, Policies...>& queue, T item) {
    return queue.try_push(new T(item));
  }

  template <class T, class... Policies>
  bool try_pop(xenium::kirsch_bounded_kfifo_queue<T*, Policies...>& queue, T& item) {
    T* value;
    auto result = queue.try_pop(value);
    if (result) {
      item = *value;
      delete value;
    }
    return result;
  }
}
#endif

#ifdef WITH_LIBCDS
#include <cds/gc/hp.h>
#include <cds/gc/dhp.h>
#include <cds/gc/nogc.h>

template<class GC> struct garbage_collector;
template<> struct garbage_collector<cds::gc::HP> {
  static constexpr const char* type() { return "HP"; }
};
template<> struct garbage_collector<cds::gc::DHP> {
  static constexpr const char* type() { return "DHP"; }
};
template<> struct garbage_collector<cds::gc::nogc> {
  static constexpr const char* type() { return "nogc"; }
};
#endif

#ifdef WITH_CDS_MSQUEUE
#include <cds/container/msqueue.h>

template <class GC, class T, class Traits>
struct descriptor<cds::container::MSQueue<GC, T, Traits>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "cds::MSQueue");
    pt.put("gc", garbage_collector<GC>::type());
    return pt;
  }
};

// libcds does not have a reclaimer, so we define
// empty dummy types as region_guard placeholder.
  
template <class GC, class T, class Traits>
struct region_guard<cds::container::MSQueue<GC, T, Traits>> {
  struct type{};
};

namespace {
  template <class GC, class T, class Traits>
  bool try_push(cds::container::MSQueue<GC, T, Traits>& queue, T item) {
    queue.push(std::move(item));
    return true;
  }

  template <class GC, class T, class Traits>
  bool try_pop(cds::container::MSQueue<GC, T, Traits>& queue, T& item) {
    return queue.pop(item);
  }
}
#endif

#ifdef WITH_CDS_BASKET_QUEUE
#include <cds/container/basket_queue.h>

template <class GC, class T, class Traits>
struct descriptor<cds::container::BasketQueue<GC, T, Traits>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "cds::BasketQueue");
    pt.put("gc", garbage_collector<GC>::type());
    return pt;
  }
};

template <class GC, class T, class Traits>
struct region_guard<cds::container::BasketQueue<GC, T, Traits>> {
  struct type{};
};

namespace {
  template <class GC, class T, class Traits>
  bool try_push(cds::container::BasketQueue<GC, T, Traits>& queue, T item) {
    queue.push(std::move(item));
    return true;
  }

  template <class GC, class T, class Traits>
  bool try_pop(cds::container::BasketQueue<GC, T, Traits>& queue, T& item) {
    return queue.pop(item);
  }
}
#endif

#ifdef WITH_CDS_SEGMENTED_QUEUE
#include <cds/container/segmented_queue.h>

template <class GC, class T, class Traits>
struct queue_builder<cds::container::SegmentedQueue<GC, T, Traits>> {
  static auto create(const boost::property_tree::ptree& config) {
    auto nQuasiFactor = config.get<size_t>("nQuasiFactor");
    return std::make_unique<cds::container::SegmentedQueue<GC, T, Traits>>(nQuasiFactor);
  }
};

template <class GC, class T, class Traits>
struct descriptor<cds::container::SegmentedQueue<GC, T, Traits>> {
  static boost::property_tree::ptree generate() {
    boost::property_tree::ptree pt;
    pt.put("type", "cds::SegmentedQueue");
    pt.put("gc", garbage_collector<GC>::type());
    pt.put("nQuasiFactor", DYNAMIC_PARAM);
    return pt;
  }
};

template <class GC, class T, class Traits>
struct region_guard<cds::container::SegmentedQueue<GC, T, Traits>> {
  struct type{};
};

namespace {
  template <class GC, class T, class Traits>
  bool try_push(cds::container::SegmentedQueue<GC, T, Traits>& queue, T item) {
    queue.push(std::move(item));
    return true;
  }

  template <class GC, class T, class Traits>
  bool try_pop(cds::container::SegmentedQueue<GC, T, Traits>& queue, T& item) {
    return queue.pop(item);
  }
}
#endif
