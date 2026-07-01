#pragma once

#include "lob/Types.hpp"

#include <cstddef>

namespace lob {

// Intrusive list node: Order with its own prev/next so the list never
// owns node memory. Backed by OrderPool.
struct OrderNode : public Order {
    OrderNode* prev = nullptr;
    OrderNode* next = nullptr;
};

// FIFO queue of orders at one price. The level never allocates — nodes
// are acquired from / released to an OrderPool by the caller.
// erase(node) is O(1) given the pointer, which is what makes cancel-
// by-id O(1) end to end.
class PriceLevel {
public:
    PriceLevel() = default;
    explicit PriceLevel(Price price) : price_(price) {}

    [[nodiscard]] Price price() const { return price_; }
    [[nodiscard]] bool empty() const { return head_ == nullptr; }
    [[nodiscard]] std::size_t order_count() const { return order_count_; }
    [[nodiscard]] Qty total_quantity() const { return total_quantity_; }

    void push_back(OrderNode* n) {
        n->prev = tail_;
        n->next = nullptr;
        if (tail_) tail_->next = n;
        else       head_ = n;
        tail_ = n;
        ++order_count_;
        total_quantity_ += n->remaining;
    }

    [[nodiscard]] OrderNode* front() { return head_; }
    [[nodiscard]] const OrderNode* front() const { return head_; }

    void pop_front() {
        OrderNode* n = head_;
        if (!n) return;
        head_ = n->next;
        if (head_) head_->prev = nullptr;
        else       tail_ = nullptr;
        --order_count_;
        total_quantity_ -= n->remaining;
        n->prev = n->next = nullptr;
    }

    void erase(OrderNode* n) {
        if (n->prev) n->prev->next = n->next;
        else         head_ = n->next;
        if (n->next) n->next->prev = n->prev;
        else         tail_ = n->prev;
        --order_count_;
        total_quantity_ -= n->remaining;
        n->prev = n->next = nullptr;
    }

    void reduce_front(Qty q) {
        total_quantity_ -= q;
        head_->remaining -= q;
    }

private:
    Price price_{};
    OrderNode* head_ = nullptr;
    OrderNode* tail_ = nullptr;
    std::size_t order_count_ = 0;
    Qty total_quantity_ = 0;
};

} // namespace lob
