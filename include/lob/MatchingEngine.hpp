#pragma once

#include "lob/OrderBook.hpp"
#include "lob/Types.hpp"

namespace lob {

class MatchingEngine {
public:
    explicit MatchingEngine(OrderBook& book) : book_(book) {}

    EventList submit_limit(OrderId id, Side side, Price price, Qty quantity);
    EventList submit_market(OrderId id, Side side, Qty quantity);
    EventList cancel(OrderId id);

    [[nodiscard]] const OrderBook& book() const { return book_; }

private:
    // Loop shared by limit and market. Walks the opposite side best-first,
    // filling `incoming` until it's done, the side is empty, or — for
    // limit orders only — the next resting price stops crossing.
    void match_against(Order& incoming, Side opposite_side, EventList& events);

    OrderBook& book_;
    Sequence next_sequence_ = 0;
};

} // namespace lob
