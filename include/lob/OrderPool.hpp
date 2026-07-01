#pragma once

#include "lob/PriceLevel.hpp"

#include <cstddef>
#include <deque>

namespace lob {

// Preallocated arena for OrderNode storage. std::deque gives stable
// pointers across growth (the order index and the per-level links hold
// OrderNode* for the node's lifetime) while growing in chunks instead
// of reallocating like vector. The free list reuses the node's own
// `next` field — a released node isn't in any price level, so its
// links are otherwise unused.
class OrderPool {
public:
    OrderPool() = default;
    OrderPool(const OrderPool&) = delete;
    OrderPool& operator=(const OrderPool&) = delete;

    [[nodiscard]] OrderNode* acquire() {
        OrderNode* n;
        if (free_head_) {
            n = free_head_;
            free_head_ = n->next;
        } else {
            storage_.emplace_back();
            n = &storage_.back();
        }
        n->prev = nullptr;
        n->next = nullptr;
        return n;
    }

    void release(OrderNode* n) {
        n->next = free_head_;
        free_head_ = n;
    }

    [[nodiscard]] std::size_t storage_size() const { return storage_.size(); }

private:
    std::deque<OrderNode> storage_;
    OrderNode* free_head_ = nullptr;
};

} // namespace lob
