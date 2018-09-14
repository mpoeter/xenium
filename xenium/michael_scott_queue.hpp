#ifndef XENIUM_MICHAEL_SCOTT_QUEUE_HPP
#define XENIUM_MICHAEL_SCOTT_QUEUE_HPP

#include "reclamation/acquire_guard.hpp"

#include "backoff.hpp"

namespace xenium {

template <class T, class Reclaimer, class Backoff = no_backoff>
class michael_scott_queue {
public:
  michael_scott_queue();
  ~michael_scott_queue();

  void enqueue(T value);
  bool try_dequeue(T& result);

private:
  struct node;

  using concurrent_ptr = typename Reclaimer::template concurrent_ptr<node, 0>;
  using marked_ptr = typename concurrent_ptr::marked_ptr;
  using guard_ptr = typename concurrent_ptr::guard_ptr;
  
  struct node : Reclaimer::template enable_concurrent_ptr<node>
  {
    T value;
    concurrent_ptr next;
  };
  
  alignas(64) concurrent_ptr head;
  alignas(64) concurrent_ptr tail;
};

template <class T, class Reclaimer, class Backoff>
michael_scott_queue<T, Reclaimer, Backoff>::michael_scott_queue()
{
  auto n = new node();
  head.store(n, std::memory_order_relaxed);
  tail.store(n, std::memory_order_relaxed);
}

template <class T, class Reclaimer, class Backoff>
michael_scott_queue<T, Reclaimer, Backoff>::~michael_scott_queue()
{
  auto n = head.load(std::memory_order_relaxed);
  while (n)
  {
    auto next = n->next.load(std::memory_order_relaxed);
    delete n.get();
    n = next;
  }
}

template <class T, class Reclaimer, class Backoff>
void michael_scott_queue<T, Reclaimer, Backoff>::enqueue(T value)
{
  node* n = new node{};
  n->value = std::move(value);

  Backoff backoff;

  guard_ptr t;
  for (;;)
  {
    // Get the old tail pointer.
    t.acquire(tail, std::memory_order_relaxed);

    // Help update the tail pointer if needed.
    auto next = t->next.load(std::memory_order_relaxed);
    if (next.get() != nullptr)
    {
      marked_ptr expected(t.get());
      tail.compare_exchange_weak(expected, next, std::memory_order_relaxed);
      continue;
    }

    // Attempt to link in the new element.
    marked_ptr null{};
    // (1) - this release-CAS synchronizes-with the acquire-load (2).
    if (t->next.compare_exchange_weak(null, n, std::memory_order_release, std::memory_order_relaxed))
      break;

    backoff();
  }

  // Swing the tail to the new element.
  marked_ptr expected = t.get();
  tail.compare_exchange_strong(expected, n, std::memory_order_relaxed);
}

template <class T, class Reclaimer, class Backoff>
bool michael_scott_queue<T, Reclaimer, Backoff>::try_dequeue(T& result)
{
  Backoff backoff;

  guard_ptr h;
  for (;;)
  {
    // Get the old head and tail elements.
    h.acquire(head, std::memory_order_relaxed);

    // Get the head element's successor.
    // (2) - this acquire-load synchronizes-with the release-CAS (1).
    auto next = acquire_guard(h->next, std::memory_order_acquire);
    if (head.load(std::memory_order_relaxed).get() != h.get())
      continue;

    // If the head (dummy) element is the only one, return false to signal that
    // the operation has failed (no element has been returned).
    if (next.get() == nullptr) 
      return false;

    marked_ptr t = tail.load(std::memory_order_relaxed);

    // There are multiple elements. Help update tail if needed.
    if (h.get() == t.get())
    {
      tail.compare_exchange_weak(t, next, std::memory_order_relaxed);
      continue;
    }

    // Save the data of the head's successor. It will become the new dummy node.
    result = next->value;

    // Attempt to update the head pointer so that it points to the new dummy node.
    marked_ptr expected(h.get());
    if (head.compare_exchange_weak(expected, next, std::memory_order_relaxed))
      break;

    backoff();
  }

  // The old dummy node has been unlinked, so reclaim it.
  h.reclaim();

  return true;
}}

#endif
