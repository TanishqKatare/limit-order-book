#include "TestFramework.hpp"

#include "lob/MatchingEngine.hpp"
#include "lob/OrderBook.hpp"

using namespace lob;

namespace {

int count_trades(const EventList& events) {
    int n = 0;
    for (const auto& e : events) if (e.type == EventType::Trade) ++n;
    return n;
}

const Event* find_first(const EventList& events, EventType type) {
    for (const auto& e : events) if (e.type == type) return &e;
    return nullptr;
}

} // namespace

TEST_CASE("empty book has no best bid or ask") {
    OrderBook book;
    EXPECT_TRUE(!book.best_bid().has_value());
    EXPECT_TRUE(!book.best_ask().has_value());
}

TEST_CASE("market order against an empty book is rejected, not crashed") {
    OrderBook book;
    MatchingEngine engine(book);

    auto events = engine.submit_market(1, Side::Buy, 10);
    EXPECT_EQ(events.size(), std::size_t{1});
    EXPECT_TRUE(events[0].type == EventType::MarketRejected);
}

TEST_CASE("a limit order with no crossing liquidity rests in the book") {
    OrderBook book;
    MatchingEngine engine(book);

    auto events = engine.submit_limit(1, Side::Buy, 100, 10);

    EXPECT_EQ(count_trades(events), 0);
    EXPECT_TRUE(find_first(events, EventType::Accepted) != nullptr);
    EXPECT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), Price{100});

    const Order* resting = book.find(1);
    EXPECT_TRUE(resting != nullptr);
    EXPECT_EQ(resting->remaining, Qty{10});
}

TEST_CASE("an exactly-matching limit order fully fills both sides") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Sell, 100, 10);
    auto events = engine.submit_limit(2, Side::Buy, 100, 10);

    EXPECT_EQ(count_trades(events), 1);
    const Event* trade_event = find_first(events, EventType::Trade);
    EXPECT_TRUE(trade_event != nullptr);
    EXPECT_EQ(trade_event->trade.maker_order_id, OrderId{1});
    EXPECT_EQ(trade_event->trade.taker_order_id, OrderId{2});
    EXPECT_EQ(trade_event->trade.price, Price{100});
    EXPECT_EQ(trade_event->trade.quantity, Qty{10});

    EXPECT_TRUE(!book.best_bid().has_value());
    EXPECT_TRUE(!book.best_ask().has_value());
    EXPECT_TRUE(book.find(1) == nullptr);
    EXPECT_TRUE(book.find(2) == nullptr);
}

TEST_CASE("incoming order smaller than the resting order produces a partial fill") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Sell, 100, 10);
    auto events = engine.submit_limit(2, Side::Buy, 100, 4);

    EXPECT_EQ(count_trades(events), 1);
    EXPECT_EQ(find_first(events, EventType::Trade)->trade.quantity, Qty{4});

    EXPECT_TRUE(book.find(2) == nullptr);

    const Order* maker = book.find(1);
    EXPECT_TRUE(maker != nullptr);
    EXPECT_EQ(maker->remaining, Qty{6});
    EXPECT_EQ(book.best_ask().value(), Price{100});
}

TEST_CASE("incoming order larger than the resting order fills it and rests the remainder") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Sell, 100, 4);
    auto events = engine.submit_limit(2, Side::Buy, 100, 10);

    EXPECT_EQ(count_trades(events), 1);
    EXPECT_EQ(find_first(events, EventType::Trade)->trade.quantity, Qty{4});
    EXPECT_TRUE(find_first(events, EventType::Accepted) != nullptr);

    EXPECT_TRUE(book.find(1) == nullptr);

    const Order* taker = book.find(2);
    EXPECT_TRUE(taker != nullptr);
    EXPECT_EQ(taker->remaining, Qty{6});
    EXPECT_EQ(book.best_bid().value(), Price{100});
}

TEST_CASE("a large marketable limit order walks through multiple ask levels") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Sell, 100, 5);
    engine.submit_limit(2, Side::Sell, 101, 5);
    engine.submit_limit(3, Side::Sell, 102, 5);

    // Buy 12 @ 102 sweeps 5@100, 5@101, then 2@102 — leaving 3 at the third level.
    auto events = engine.submit_limit(4, Side::Buy, 102, 12);

    EXPECT_EQ(count_trades(events), 3);
    EXPECT_EQ(events[0].trade.price, Price{100});
    EXPECT_EQ(events[0].trade.quantity, Qty{5});
    EXPECT_EQ(events[1].trade.price, Price{101});
    EXPECT_EQ(events[1].trade.quantity, Qty{5});
    EXPECT_EQ(events[2].trade.price, Price{102});
    EXPECT_EQ(events[2].trade.quantity, Qty{2});

    EXPECT_TRUE(book.find(1) == nullptr);
    EXPECT_TRUE(book.find(2) == nullptr);

    const Order* level3_maker = book.find(3);
    EXPECT_TRUE(level3_maker != nullptr);
    EXPECT_EQ(level3_maker->remaining, Qty{3});

    EXPECT_EQ(book.best_ask().value(), Price{102});
}

TEST_CASE("a limit order stops walking once price no longer crosses") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Sell, 100, 5);
    engine.submit_limit(2, Side::Sell, 105, 5);

    auto events = engine.submit_limit(3, Side::Buy, 100, 10);

    EXPECT_EQ(count_trades(events), 1);
    EXPECT_EQ(events[0].trade.price, Price{100});
    EXPECT_EQ(events[0].trade.quantity, Qty{5});

    EXPECT_EQ(book.best_bid().value(), Price{100});
    EXPECT_EQ(book.best_ask().value(), Price{105});
    const Order* untouched = book.find(2);
    EXPECT_TRUE(untouched != nullptr);
    EXPECT_EQ(untouched->remaining, Qty{5});
}

TEST_CASE("orders at the same price fill in strict arrival order (FIFO)") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Sell, 100, 5);
    engine.submit_limit(2, Side::Sell, 100, 5);

    auto events = engine.submit_limit(3, Side::Buy, 100, 7);

    EXPECT_EQ(count_trades(events), 2);
    EXPECT_EQ(events[0].trade.maker_order_id, OrderId{1});
    EXPECT_EQ(events[0].trade.quantity, Qty{5});
    EXPECT_EQ(events[1].trade.maker_order_id, OrderId{2});
    EXPECT_EQ(events[1].trade.quantity, Qty{2});

    const Order* remaining_order2 = book.find(2);
    EXPECT_TRUE(remaining_order2 != nullptr);
    EXPECT_EQ(remaining_order2->remaining, Qty{3});
}

TEST_CASE("cancelling an untouched resting order removes it from the book") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Buy, 100, 10);
    auto events = engine.cancel(1);

    EXPECT_EQ(events.size(), std::size_t{1});
    EXPECT_TRUE(events[0].type == EventType::Cancelled);
    EXPECT_TRUE(book.find(1) == nullptr);
    EXPECT_TRUE(!book.best_bid().has_value());
}

TEST_CASE("cancelling a partially-filled order removes only the remaining quantity") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Sell, 100, 10);
    engine.submit_limit(2, Side::Buy, 100, 4);

    const Order* before_cancel = book.find(1);
    EXPECT_TRUE(before_cancel != nullptr);
    EXPECT_EQ(before_cancel->remaining, Qty{6});

    auto events = engine.cancel(1);
    EXPECT_TRUE(events[0].type == EventType::Cancelled);
    EXPECT_TRUE(book.find(1) == nullptr);
    EXPECT_TRUE(!book.best_ask().has_value());
}

TEST_CASE("cancelling an unknown order id is rejected, not an error") {
    OrderBook book;
    MatchingEngine engine(book);

    auto events = engine.cancel(999);
    EXPECT_EQ(events.size(), std::size_t{1});
    EXPECT_TRUE(events[0].type == EventType::CancelRejected);
}

TEST_CASE("cancelling an order twice rejects the second cancel") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Buy, 100, 10);
    engine.cancel(1);
    auto second = engine.cancel(1);

    EXPECT_TRUE(second[0].type == EventType::CancelRejected);
}

TEST_CASE("a fully-filled order can no longer be cancelled") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Sell, 100, 10);
    engine.submit_limit(2, Side::Buy, 100, 10);

    auto events = engine.cancel(1);
    EXPECT_TRUE(events[0].type == EventType::CancelRejected);
}

TEST_CASE("a market order fully fills against resting liquidity at multiple prices") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Sell, 100, 5);
    engine.submit_limit(2, Side::Sell, 101, 5);

    auto events = engine.submit_market(3, Side::Buy, 8);

    EXPECT_EQ(count_trades(events), 2);
    EXPECT_EQ(events[0].trade.price, Price{100});
    EXPECT_EQ(events[0].trade.quantity, Qty{5});
    EXPECT_EQ(events[1].trade.price, Price{101});
    EXPECT_EQ(events[1].trade.quantity, Qty{3});

    EXPECT_TRUE(book.find(3) == nullptr);
    const Order* leftover_maker = book.find(2);
    EXPECT_TRUE(leftover_maker != nullptr);
    EXPECT_EQ(leftover_maker->remaining, Qty{2});
}

TEST_CASE("a market order with insufficient liquidity fills what it can and drops the rest") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Sell, 100, 3);

    auto events = engine.submit_market(2, Side::Buy, 10);

    EXPECT_EQ(count_trades(events), 1);
    EXPECT_EQ(events[0].trade.quantity, Qty{3});

    EXPECT_TRUE(!book.best_ask().has_value());
    EXPECT_TRUE(book.find(2) == nullptr);
}

TEST_CASE("a market sell order matches against the bid side") {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit_limit(1, Side::Buy, 100, 5);
    auto events = engine.submit_market(2, Side::Sell, 5);

    EXPECT_EQ(count_trades(events), 1);
    EXPECT_EQ(events[0].trade.price, Price{100});
    EXPECT_TRUE(!book.best_bid().has_value());
}

TEST_CASE("submitting a zero or negative quantity limit order throws") {
    OrderBook book;
    MatchingEngine engine(book);
    EXPECT_THROWS(engine.submit_limit(1, Side::Buy, 100, 0));
    EXPECT_THROWS(engine.submit_limit(2, Side::Buy, 100, -5));
}

TEST_CASE("submitting a non-positive price limit order throws") {
    OrderBook book;
    MatchingEngine engine(book);
    EXPECT_THROWS(engine.submit_limit(1, Side::Buy, 0, 10));
    EXPECT_THROWS(engine.submit_limit(2, Side::Buy, -1, 10));
}

TEST_CASE("reusing an order id that is still resting in the book throws") {
    OrderBook book;
    MatchingEngine engine(book);
    engine.submit_limit(1, Side::Buy, 100, 10);
    EXPECT_THROWS(engine.submit_limit(1, Side::Sell, 200, 5));
}

TEST_CASE("an order id can be reused once the original order is gone") {
    OrderBook book;
    MatchingEngine engine(book);
    engine.submit_limit(1, Side::Buy, 100, 10);
    engine.cancel(1);
    auto events = engine.submit_limit(1, Side::Sell, 200, 5);
    EXPECT_TRUE(find_first(events, EventType::Accepted) != nullptr);
}

int main() {
    return ::lob::test::run_all();
}
