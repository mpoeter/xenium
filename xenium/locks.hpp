//
// Copyright (c) 2018 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#ifndef XENIUM_LOCKS_HPP
#define XENIUM_LOCKS_HPP

#include <atomic>
#include <cstdint>

namespace xenium { namespace locks {
  /**
   * @brief A simple test-and-test-and-set lock.
   */
  template <class... Policies>
  struct ttas {
    bool try_lock() {
      int expected = 0;
      return lock.compare_exchange_strong(expected, 1, std::memory_order_acquire, std::memory_order_relaxed);
    }

    void lock() {
      for (;;) {
        while (lock.load(std::memory_order_relaxed) != 0)
          ;

        int expected = 0;
        if (lock.compare_exchange_weak(expected, 1, std::memory_order_acquire, std::memory_order_relaxed))
          break;

        // TODO - backoff
      }
    }
    void unlock() {
      assert(lock.load() == 1);
      lock.store(0, std::memory_order_release);
    }
  private:
    std::atomic<int> lock;
  };

  /**
   * @brief A simple ticket lock.
   */
  template <class... Policies>
  struct ticket {
    void lock() {
      auto my_ticket = next_ticket.fetch_add(std::memory_order_relaxed);
      while (active_ticket.load(std::memory_order_acquire) != t)
        ; // TODO - backoff (possibly depending on distance between tickets)
    }
    void unlock() {
      auto t = active_ticket.load(std::memory_order_relaxed);
      active_ticket.store(t + 1, std::memory_order_release);
    }
  private:
    std::atomic<std::size_t> next_ticket;
    std::atomic<std::size_t> active_ticket;
  };
}}

#endif
