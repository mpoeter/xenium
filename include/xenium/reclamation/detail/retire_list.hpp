//
// Copyright (c) 2018-2020 Manuel Pöter.
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
//

#pragma once

#include <xenium/detail/port.hpp>
#include <xenium/reclamation/detail/deletable_object.hpp>

namespace xenium::reclamation::detail {
template <class Node = deletable_object>
struct retired_nodes {
  Node* first;
  Node* last;
};

template <class Node = deletable_object>
struct retire_list {
  retire_list() {
    _nodes.first = nullptr;
    _nodes.last = nullptr;
  }

  ~retire_list() { assert(_nodes.first == nullptr); }

  void push(Node* node) {
    node->next = _nodes.first;
    _nodes.first = node;
    if (_nodes.last == nullptr) {
      _nodes.last = node;
    }
  }

  retired_nodes<Node> steal() {
    auto result = _nodes;
    _nodes.first = nullptr;
    _nodes.last = nullptr;
    return result;
  };

  [[nodiscard]] bool empty() const { return _nodes.first == nullptr; }

private:
  retired_nodes<Node> _nodes;
};

template <class Node = deletable_object>
struct counting_retire_list {
  void push(Node* node) {
    list.push(node);
    ++counter;
  }

  retired_nodes<Node> steal() {
    counter = 0;
    return list.steal();
  };

  [[nodiscard]] bool empty() const { return list.empty(); }
  [[nodiscard]] std::size_t size() const { return counter; }

private:
  retire_list<Node> list;
  std::size_t counter;
};

template <class Node = deletable_object>
struct orphan_list {
  void add(retired_nodes<Node> nodes) {
    assert(nodes.first != nullptr);
    auto* h = head.load(std::memory_order_relaxed);
    do {
      nodes.last->next = h;
      // (1) - this releas-CAS synchronizes-with the acquire-exchange (2)
    } while (!head.compare_exchange_weak(h, nodes.first, std::memory_order_release, std::memory_order_relaxed));
  }

  XENIUM_FORCEINLINE Node* adopt() {
    if (head.load(std::memory_order_relaxed) == nullptr) {
      return nullptr;
    }

    // (2) - this acquire-exchange synchronizes-with the release-CAS (1)
    return head.exchange(nullptr, std::memory_order_acquire);
  };

private:
  std::atomic<Node*> head;
};
} // namespace xenium::reclamation::detail