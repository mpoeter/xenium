#include "benchmark.hpp"
#include "descriptor.hpp"
#include "reclaimers.hpp"

template <class T>
struct queue_builder {
  static auto create(const tao::config::value&) { return std::make_unique<T>(); }
};

#ifdef WITH_RAMALHETE_QUEUE
  #include <xenium/ramalhete_queue.hpp>

template <class T, class... Policies>
struct descriptor<xenium::ramalhete_queue<T, Policies...>> {
  static tao::json::value generate() {
    using queue = xenium::ramalhete_queue<T, Policies...>;
    return {{"type", "ramalhete_queue"}, {"reclaimer", descriptor<typename queue::reclaimer>::generate()}};
  }
};

namespace { // NOLINT
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
} // namespace
#endif

#ifdef WITH_MICHAEL_SCOTT_QUEUE
  #include <xenium/michael_scott_queue.hpp>

template <class T, class... Policies>
struct descriptor<xenium::michael_scott_queue<T, Policies...>> {
  static tao::json::value generate() {
    using queue = xenium::michael_scott_queue<T, Policies...>;
    return {{"type", "michael_scott_queue"}, {"reclaimer", descriptor<typename queue::reclaimer>::generate()}};
  }
};

namespace { // NOLINT
template <class T, class... Policies>
bool try_push(xenium::michael_scott_queue<T, Policies...>& queue, T item) {
  queue.push(std::move(item));
  return true;
}

template <class T, class... Policies>
bool try_pop(xenium::michael_scott_queue<T, Policies...>& queue, T& item) {
  return queue.try_pop(item);
}
} // namespace
#endif

#ifdef WITH_VYUKOV_BOUNDED_QUEUE
  #include <xenium/vyukov_bounded_queue.hpp>

template <class T, class... Policies>
struct descriptor<xenium::vyukov_bounded_queue<T, Policies...>> {
  static tao::json::value generate() {
    using queue = xenium::vyukov_bounded_queue<T, Policies...>;
    return {{"type", "vyukov_bounded_queue"}, {"weak", queue::default_to_weak}, {"size", DYNAMIC_PARAM}};
  }
};

template <class T, class... Policies>
struct queue_builder<xenium::vyukov_bounded_queue<T, Policies...>> {
  static auto create(const tao::config::value& config) {
    auto size = config.as<size_t>("size");
    if (!xenium::utils::is_power_of_two(size)) {
      throw std::runtime_error("vyukov_bounded_queue size must be a power of two");
    }
    return std::make_unique<xenium::vyukov_bounded_queue<T, Policies...>>(size);
  }
};

template <class T, class... Policies>
struct region_guard<xenium::vyukov_bounded_queue<T, Policies...>> {
  // vyukov_bounded_queue does not have a reclaimer, so we define an
  // empty dummy type as region_guard placeholder.
  struct type {};
};

namespace { // NOLINT
template <class T, class... Policies>
bool try_push(xenium::vyukov_bounded_queue<T, Policies...>& queue, T item) {
  return queue.try_push(std::move(item));
}

template <class T, class... Policies>
bool try_pop(xenium::vyukov_bounded_queue<T, Policies...>& queue, T& item) {
  return queue.try_pop(item);
}
} // namespace
#endif

#ifdef WITH_KIRSCH_KFIFO_QUEUE
  #include <xenium/kirsch_kfifo_queue.hpp>

template <class T, class... Policies>
struct descriptor<xenium::kirsch_kfifo_queue<T, Policies...>> {
  static tao::json::value generate() {
    using queue = xenium::kirsch_kfifo_queue<T, Policies...>;
    return {{"type", "kirsch_kfifo_queue"},
            {"k", DYNAMIC_PARAM},
            {"reclaimer", descriptor<typename queue::reclaimer>::generate()}};
  }
};

template <class T, class... Policies>
struct queue_builder<xenium::kirsch_kfifo_queue<T, Policies...>> {
  static auto create(const tao::config::value& config) {
    auto k = config.as<size_t>("k");
    return std::make_unique<xenium::kirsch_kfifo_queue<T, Policies...>>(k);
  }
};

namespace { // NOLINT
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
} // namespace
#endif

#ifdef WITH_KIRSCH_BOUNDED_KFIFO_QUEUE
  #include <xenium/kirsch_bounded_kfifo_queue.hpp>

template <class T, class... Policies>
struct descriptor<xenium::kirsch_bounded_kfifo_queue<T, Policies...>> {
  static tao::json::value generate() {
    // TODO - consider padding parameter
    // using queue = xenium::kirsch_bounded_kfifo_queue<T, Policies...>;
    return {{"type", "kirsch_bounded_kfifo_queue"}, {"k", DYNAMIC_PARAM}, {"segments", DYNAMIC_PARAM}};
  }
};

template <class T, class... Policies>
struct queue_builder<xenium::kirsch_bounded_kfifo_queue<T, Policies...>> {
  static auto create(const tao::config::value& config) {
    auto k = config.as<size_t>("k");
    auto segments = config.as<size_t>("segments");
    return std::make_unique<xenium::kirsch_bounded_kfifo_queue<T, Policies...>>(k, segments);
  }
};

template <class T, class... Policies>
struct region_guard<xenium::kirsch_bounded_kfifo_queue<T, Policies...>> {
  // kirsch_bounded_kfifo_queue does not have a reclaimer, so we define an
  // empty dummy type as region_guard placeholder.
  struct type {};
};

namespace { // NOLINT
template <class T, class... Policies>
bool try_push(xenium::kirsch_bounded_kfifo_queue<T*, Policies...>& queue, T item) {
  return queue.try_push(new T(item)); // NOLINT
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
} // namespace
#endif

#ifdef WITH_NIKOLAEV_BOUNDED_QUEUE
  #include <xenium/nikolaev_bounded_queue.hpp>

template <class T, class... Policies>
struct descriptor<xenium::nikolaev_bounded_queue<T, Policies...>> {
  static tao::json::value generate() { return {{"type", "nikolaev_bounded_queue"}, {"capacity", DYNAMIC_PARAM}}; }
};

template <class T, class... Policies>
struct queue_builder<xenium::nikolaev_bounded_queue<T, Policies...>> {
  static auto create(const tao::config::value& config) {
    auto capacity = config.as<size_t>("capacity");
    return std::make_unique<xenium::nikolaev_bounded_queue<T, Policies...>>(capacity);
  }
};

template <class T, class... Policies>
struct region_guard<xenium::nikolaev_bounded_queue<T, Policies...>> {
  // nikolaev_bounded_queue does not have a reclaimer, so we define an
  // empty dummy type as region_guard placeholder.
  struct type {};
};

namespace { // NOLINT
template <class T, class... Policies>
bool try_push(xenium::nikolaev_bounded_queue<T, Policies...>& queue, T item) {
  return queue.try_push(std::move(item));
}

template <class T, class... Policies>
bool try_pop(xenium::nikolaev_bounded_queue<T, Policies...>& queue, T& item) {
  return queue.try_pop(item);
}
} // namespace
#endif

#ifdef WITH_LIBCDS
  #include <cds/gc/dhp.h>
  #include <cds/gc/hp.h>
  #include <cds/gc/nogc.h>

template <class GC>
struct garbage_collector;
template <>
struct garbage_collector<cds::gc::HP> {
  static constexpr const char* type() { return "HP"; }
};
template <>
struct garbage_collector<cds::gc::DHP> {
  static constexpr const char* type() { return "DHP"; }
};
template <>
struct garbage_collector<cds::gc::nogc> {
  static constexpr const char* type() { return "nogc"; }
};
#endif

#ifdef WITH_CDS_MSQUEUE
  #include <cds/container/msqueue.h>

template <class GC, class T, class Traits>
struct descriptor<cds::container::MSQueue<GC, T, Traits>> {
  static tao::json::value generate() { return {{"type", "cds::MSQueue"}, {"gc", garbage_collector<GC>::type()}}; }
};

// libcds does not have a reclaimer, so we define
// empty dummy types as region_guard placeholder.

template <class GC, class T, class Traits>
struct region_guard<cds::container::MSQueue<GC, T, Traits>> {
  struct type {};
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
} // namespace
#endif

#ifdef WITH_CDS_BASKET_QUEUE
  #include <cds/container/basket_queue.h>

template <class GC, class T, class Traits>
struct descriptor<cds::container::BasketQueue<GC, T, Traits>> {
  static tao::json::value generate() { return {{"type", "cds::BasketQueue"}, {"gc", garbage_collector<GC>::type()}}; }
};

template <class GC, class T, class Traits>
struct region_guard<cds::container::BasketQueue<GC, T, Traits>> {
  struct type {};
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
} // namespace
#endif

#ifdef WITH_CDS_SEGMENTED_QUEUE
  #include <cds/container/segmented_queue.h>

template <class GC, class T, class Traits>
struct queue_builder<cds::container::SegmentedQueue<GC, T, Traits>> {
  static auto create(const tao::config::value& config) {
    auto nQuasiFactor = config.as<size_t>("nQuasiFactor");
    return std::make_unique<cds::container::SegmentedQueue<GC, T, Traits>>(nQuasiFactor);
  }
};

template <class GC, class T, class Traits>
struct descriptor<cds::container::SegmentedQueue<GC, T, Traits>> {
  static tao::json::value generate() {
    return {{"type", "cds::SegmentedQueue"}, {"gc", garbage_collector<GC>::type()}, {"nQuasiFactor", DYNAMIC_PARAM}};
  }
};

template <class GC, class T, class Traits>
struct region_guard<cds::container::SegmentedQueue<GC, T, Traits>> {
  struct type {};
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
} // namespace
#endif

#ifdef WITH_BOOST_LOCKFREE_QUEUE
  #include <boost/lockfree/queue.hpp>

template <class T, class... Policies>
struct queue_builder<boost::lockfree::queue<T, Policies...>> {
  static auto create(const tao::config::value& config) {
    auto size = config.as<size_t>("size");
    return std::make_unique<boost::lockfree::queue<T, Policies...>>(size);
  }
};

template <class T, class... Policies>
struct descriptor<boost::lockfree::queue<T, Policies...>> {
  static tao::json::value generate() { return {{"type", "boost::lockfree::queue"}, {"size", DYNAMIC_PARAM}}; }
};

template <class T, class... Policies>
struct region_guard<boost::lockfree::queue<T, Policies...>> {
  struct type {};
};

namespace {
template <class T, class... Policies>
bool try_push(boost::lockfree::queue<T, Policies...>& queue, T item) {
  return queue.push(std::move(item));
}

template <class T, class... Policies>
bool try_pop(boost::lockfree::queue<T, Policies...>& queue, T& item) {
  return queue.pop(item);
}
} // namespace
#endif
