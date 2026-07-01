#include "lob/OrderBook.hpp"

#include <stdexcept>

namespace lob {

OrderBook::OrderBook(Price min_price, Price max_price)
    : min_price_(min_price), max_price_(max_price) {
    if (min_price <= 0 || max_price < min_price) {
        throw std::invalid_argument("OrderBook: invalid price range");
    }
    const std::size_t n = static_cast<std::size_t>(max_price - min_price + 1);
    if (n > kMaxLevels) {
        throw std::invalid_argument("OrderBook: price range too large");
    }
    bid_levels_.reserve(n);
    ask_levels_.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Price p = min_price + static_cast<Price>(i);
        bid_levels_.emplace_back(p);
        ask_levels_.emplace_back(p);
    }
}

std::optional<Price> OrderBook::best_bid() const {
    return best_bid_ == 0 ? std::nullopt : std::optional<Price>(best_bid_);
}
std::optional<Price> OrderBook::best_ask() const {
    return best_ask_ == 0 ? std::nullopt : std::optional<Price>(best_ask_);
}

bool OrderBook::empty(Side side) const {
    return side == Side::Buy ? best_bid_ == 0 : best_ask_ == 0;
}

const Order* OrderBook::find(OrderId id) const {
    auto it = index_.find(id);
    if (it == index_.end()) return nullptr;
    return it->second.node;
}

void OrderBook::add_resting_order(Order order) {
    if (order.price < min_price_ || order.price > max_price_) {
        throw std::out_of_range("OrderBook: price outside configured tick range");
    }

    OrderNode* node = pool_.acquire();
    static_cast<Order&>(*node) = order;

    const std::size_t pidx = index_for(order.price);
    if (order.side == Side::Buy) {
        PriceLevel& level = bid_levels_[pidx];
        const bool was_empty = level.empty();
        level.push_back(node);
        if (was_empty) ++bid_level_count_;
        if (order.price > best_bid_) best_bid_ = order.price;
    } else {
        PriceLevel& level = ask_levels_[pidx];
        const bool was_empty = level.empty();
        level.push_back(node);
        if (was_empty) ++ask_level_count_;
        if (best_ask_ == 0 || order.price < best_ask_) best_ask_ = order.price;
    }

    index_[order.id] = OrderLocation{order.side, order.price, node};
}

Order* OrderBook::best_front(Side side) {
    if (side == Side::Buy) {
        if (best_bid_ == 0) return nullptr;
        return bid_levels_[index_for(best_bid_)].front();
    }
    if (best_ask_ == 0) return nullptr;
    return ask_levels_[index_for(best_ask_)].front();
}

void OrderBook::fill_best_front(Side side, Qty qty) {
    if (side == Side::Buy) {
        PriceLevel& level = bid_levels_[index_for(best_bid_)];
        OrderNode* node = level.front();
        const OrderId filled_id = node->id;

        level.reduce_front(qty);
        if (node->is_filled()) {
            level.pop_front();
            index_.erase(filled_id);
            pool_.release(node);
        }
        if (level.empty()) {
            --bid_level_count_;
            rescan_best_bid_after_empty(best_bid_);
        }
    } else {
        PriceLevel& level = ask_levels_[index_for(best_ask_)];
        OrderNode* node = level.front();
        const OrderId filled_id = node->id;

        level.reduce_front(qty);
        if (node->is_filled()) {
            level.pop_front();
            index_.erase(filled_id);
            pool_.release(node);
        }
        if (level.empty()) {
            --ask_level_count_;
            rescan_best_ask_after_empty(best_ask_);
        }
    }
}

bool OrderBook::cancel(OrderId id) {
    auto idx_it = index_.find(id);
    if (idx_it == index_.end()) return false;

    const OrderLocation loc = idx_it->second;
    index_.erase(idx_it);

    const std::size_t pidx = index_for(loc.price);
    if (loc.side == Side::Buy) {
        PriceLevel& level = bid_levels_[pidx];
        level.erase(loc.node);
        if (level.empty()) {
            --bid_level_count_;
            if (loc.price == best_bid_) rescan_best_bid_after_empty(best_bid_);
        }
    } else {
        PriceLevel& level = ask_levels_[pidx];
        level.erase(loc.node);
        if (level.empty()) {
            --ask_level_count_;
            if (loc.price == best_ask_) rescan_best_ask_after_empty(best_ask_);
        }
    }
    pool_.release(loc.node);
    return true;
}

// Linear scan from the just-emptied best to find the next non-empty
// level. Cheap for dense books; pathological for sparse ones — a
// hierarchical bitmap + bit-scan would make this O(1) worst case.
void OrderBook::rescan_best_bid_after_empty(Price emptied_price) {
    for (Price p = emptied_price - 1; p >= min_price_; --p) {
        if (!bid_levels_[index_for(p)].empty()) {
            best_bid_ = p;
            return;
        }
    }
    best_bid_ = 0;
}

void OrderBook::rescan_best_ask_after_empty(Price emptied_price) {
    for (Price p = emptied_price + 1; p <= max_price_; ++p) {
        if (!ask_levels_[index_for(p)].empty()) {
            best_ask_ = p;
            return;
        }
    }
    best_ask_ = 0;
}

} // namespace lob
