#pragma once

#include "lob/OrderPool.hpp"
#include "lob/PriceLevel.hpp"
#include "lob/Types.hpp"

#include <cstddef>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lob {

// The book is a data structure with price-time-priority semantics.
// Matching logic (crossing checks, level walking, trade price) lives in
// MatchingEngine; this file exposes the primitives MatchingEngine
// composes — best_front, fill_best_front, add_resting_order, cancel.
class OrderBook {
public:
    static constexpr Price kDefaultMinPrice = 1;
    static constexpr Price kDefaultMaxPrice = 100'000;
    static constexpr std::size_t kMaxLevels = 10'000'000;

    explicit OrderBook(Price min_price = kDefaultMinPrice,
                       Price max_price = kDefaultMaxPrice);

    [[nodiscard]] std::optional<Price> best_bid() const;
    [[nodiscard]] std::optional<Price> best_ask() const;
    [[nodiscard]] bool empty(Side side) const;
    [[nodiscard]] const Order* find(OrderId id) const;

    [[nodiscard]] Price min_price() const { return min_price_; }
    [[nodiscard]] Price max_price() const { return max_price_; }
    [[nodiscard]] std::size_t bid_level_count() const { return bid_level_count_; }
    [[nodiscard]] std::size_t ask_level_count() const { return ask_level_count_; }

    // Visit non-empty levels best-first. Visitor is
    // `void(Price, const PriceLevel&)`, or return `bool` to stop early.
    template <typename F>
    void for_each_bid_level(F&& visitor,
                            std::size_t max_levels = static_cast<std::size_t>(-1)) const;
    template <typename F>
    void for_each_ask_level(F&& visitor,
                            std::size_t max_levels = static_cast<std::size_t>(-1)) const;

    void add_resting_order(Order order);
    [[nodiscard]] Order* best_front(Side side);
    void fill_best_front(Side side, Qty qty);
    bool cancel(OrderId id);

private:
    [[nodiscard]] std::size_t index_for(Price p) const {
        return static_cast<std::size_t>(p - min_price_);
    }

    void rescan_best_bid_after_empty(Price emptied_price);
    void rescan_best_ask_after_empty(Price emptied_price);

    struct OrderLocation {
        Side side{};
        Price price{};
        OrderNode* node{};
    };

    Price min_price_;
    Price max_price_;
    // Flat array indexed by (price - min_price_). O(1) level access at
    // the cost of committed memory for the whole tradable range.
    std::vector<PriceLevel> bid_levels_;
    std::vector<PriceLevel> ask_levels_;
    Price best_bid_ = 0;  // 0 = side empty (price 0 is invalid)
    Price best_ask_ = 0;
    std::size_t bid_level_count_ = 0;
    std::size_t ask_level_count_ = 0;
    OrderPool pool_;
    std::unordered_map<OrderId, OrderLocation> index_;
};

namespace detail {
template <typename F, typename... Args>
inline bool invoke_visitor(F& f, Args&&... args) {
    if constexpr (std::is_same_v<std::invoke_result_t<F&, Args...>, bool>) {
        return f(std::forward<Args>(args)...);
    } else {
        f(std::forward<Args>(args)...);
        return true;
    }
}
} // namespace detail

template <typename F>
void OrderBook::for_each_bid_level(F&& visitor, std::size_t max_levels) const {
    if (best_bid_ == 0 || max_levels == 0) return;
    std::size_t emitted = 0;
    for (Price p = best_bid_; p >= min_price_; --p) {
        const PriceLevel& lvl = bid_levels_[index_for(p)];
        if (!lvl.empty()) {
            if (!detail::invoke_visitor(visitor, p, lvl)) return;
            if (++emitted >= max_levels) return;
        }
    }
}

template <typename F>
void OrderBook::for_each_ask_level(F&& visitor, std::size_t max_levels) const {
    if (best_ask_ == 0 || max_levels == 0) return;
    std::size_t emitted = 0;
    for (Price p = best_ask_; p <= max_price_; ++p) {
        const PriceLevel& lvl = ask_levels_[index_for(p)];
        if (!lvl.empty()) {
            if (!detail::invoke_visitor(visitor, p, lvl)) return;
            if (++emitted >= max_levels) return;
        }
    }
}

} // namespace lob
