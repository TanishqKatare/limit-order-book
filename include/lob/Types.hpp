#pragma once

#include <cstdint>
#include <vector>

namespace lob {

// Integer ticks, not floating point — we need exact equality for price
// comparisons and as a key into the price ladder.
using Price = std::int64_t;
using Qty = std::int64_t;
using OrderId = std::uint64_t;
using Sequence = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

inline Side opposite(Side s) {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

enum class OrderType : std::uint8_t { Limit, Market };

struct Order {
    OrderId id{};
    Side side{};
    OrderType type{};
    Price price{};
    Qty quantity{};
    Qty remaining{};
    Sequence sequence{};

    [[nodiscard]] bool is_filled() const { return remaining == 0; }
};

struct Trade {
    OrderId taker_order_id{};
    OrderId maker_order_id{};
    Price price{};   // resting (maker) order's price — price-time priority
    Qty quantity{};
};

enum class EventType : std::uint8_t {
    Accepted,
    Trade,
    Cancelled,
    CancelRejected,
    MarketRejected,
};

struct Event {
    EventType type{};
    Trade trade{};
    OrderId order_id{};
};

using EventList = std::vector<Event>;

} // namespace lob
