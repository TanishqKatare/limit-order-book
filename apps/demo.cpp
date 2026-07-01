// Scripted walkthrough: build up a book, cross it, market-order it,
// cancel. Run it and watch the printed book state after each step.

#include "lob/BookPrinter.hpp"
#include "lob/MatchingEngine.hpp"
#include "lob/OrderBook.hpp"

#include <iostream>

using namespace lob;

namespace {

void print_events(const EventList& events) {
    for (const auto& e : events) {
        switch (e.type) {
            case EventType::Accepted:
                std::cout << "  accepted: order " << e.order_id << " now resting\n";
                break;
            case EventType::Trade:
                std::cout << "  trade: taker=" << e.trade.taker_order_id
                           << " maker=" << e.trade.maker_order_id
                           << " price=" << e.trade.price
                           << " qty=" << e.trade.quantity << '\n';
                break;
            case EventType::Cancelled:
                std::cout << "  cancelled: order " << e.order_id << '\n';
                break;
            case EventType::CancelRejected:
                std::cout << "  cancel rejected: order " << e.order_id << " not found\n";
                break;
            case EventType::MarketRejected:
                std::cout << "  market order " << e.order_id << " rejected: no liquidity\n";
                break;
        }
    }
}

} // namespace

int main() {
    OrderBook book;
    MatchingEngine engine(book);
    OrderId next_id = 1;

    std::cout << "=== Building up the book with resting limit orders ===\n";
    print_events(engine.submit_limit(next_id++, Side::Buy, 99, 10));
    print_events(engine.submit_limit(next_id++, Side::Buy, 98, 5));
    print_events(engine.submit_limit(next_id++, Side::Sell, 101, 7));
    print_events(engine.submit_limit(next_id++, Side::Sell, 102, 12));
    print_book(book, std::cout);

    std::cout << "\n=== A crossing limit buy order walks the ask side ===\n";
    print_events(engine.submit_limit(next_id++, Side::Buy, 102, 15));
    print_book(book, std::cout);

    std::cout << "\n=== A market sell order hits the bid side ===\n";
    print_events(engine.submit_market(next_id++, Side::Sell, 6));
    print_book(book, std::cout);

    std::cout << "\n=== Cancelling the remaining best bid ===\n";
    print_events(engine.cancel(1));
    print_book(book, std::cout);

    return 0;
}
