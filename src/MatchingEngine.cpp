#include "lob/MatchingEngine.hpp"

#include <stdexcept>

namespace lob {

namespace {

bool crosses(Side incoming_side, Price incoming_price, Price opposite_best) {
    return incoming_side == Side::Buy
               ? incoming_price >= opposite_best
               : incoming_price <= opposite_best;
}

} // namespace

void MatchingEngine::match_against(Order& incoming, Side opposite_side, EventList& events) {
    while (incoming.remaining > 0) {
        Order* maker = book_.best_front(opposite_side);
        if (maker == nullptr) break;

        if (incoming.type == OrderType::Limit &&
            !crosses(incoming.side, incoming.price, maker->price)) {
            break;
        }

        const Qty fill_qty = std::min(incoming.remaining, maker->remaining);
        // Read maker fields BEFORE fill_best_front, which may free the node.
        Trade trade{incoming.id, maker->id, maker->price, fill_qty};

        book_.fill_best_front(opposite_side, fill_qty);
        incoming.remaining -= fill_qty;

        events.push_back(Event{EventType::Trade, trade, 0});
    }
}

EventList MatchingEngine::submit_limit(OrderId id, Side side, Price price, Qty quantity) {
    if (quantity <= 0) throw std::invalid_argument("submit_limit: quantity must be positive");
    if (price <= 0)    throw std::invalid_argument("submit_limit: price must be positive");
    if (book_.find(id) != nullptr) throw std::invalid_argument("submit_limit: duplicate order id");

    Order incoming{id, side, OrderType::Limit, price, quantity, quantity, next_sequence_++};

    EventList events;
    match_against(incoming, opposite(side), events);

    if (incoming.remaining > 0) {
        events.push_back(Event{EventType::Accepted, Trade{}, id});
        book_.add_resting_order(std::move(incoming));
    }
    return events;
}

EventList MatchingEngine::submit_market(OrderId id, Side side, Qty quantity) {
    if (quantity <= 0) throw std::invalid_argument("submit_market: quantity must be positive");
    if (book_.find(id) != nullptr) throw std::invalid_argument("submit_market: duplicate order id");

    EventList events;
    if (book_.empty(opposite(side))) {
        events.push_back(Event{EventType::MarketRejected, Trade{}, id});
        return events;
    }

    // Market orders never rest — any unfilled remainder is dropped.
    Order incoming{id, side, OrderType::Market, 0, quantity, quantity, next_sequence_++};
    match_against(incoming, opposite(side), events);
    return events;
}

EventList MatchingEngine::cancel(OrderId id) {
    EventList events;
    events.push_back(Event{
        book_.cancel(id) ? EventType::Cancelled : EventType::CancelRejected,
        Trade{},
        id,
    });
    return events;
}

} // namespace lob
