#include "lob/BookPrinter.hpp"

#include <iomanip>
#include <vector>

namespace lob {

void print_book(const OrderBook& book, std::ostream& os, std::size_t depth) {
    struct LevelView {
        Price price;
        Qty total_qty;
        std::size_t order_count;
    };

    // Collect asks best-first, print in reverse so the best ask sits
    // closest to the spread divider.
    std::vector<LevelView> asks;
    book.for_each_ask_level(
        [&](Price p, const PriceLevel& lvl) {
            asks.push_back({p, lvl.total_quantity(), lvl.order_count()});
        },
        depth);

    os << "----- ASKS (best ask at bottom) -----\n";
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        os << "  " << std::setw(10) << it->price
           << "  qty=" << std::setw(8) << it->total_qty
           << "  orders=" << it->order_count << '\n';
    }

    const auto bid = book.best_bid();
    const auto ask = book.best_ask();
    os << "-------------- spread: ";
    if (bid && ask) {
        os << (*ask - *bid) << " (" << *bid << " / " << *ask << ")";
    } else {
        os << "n/a (one or both sides empty)";
    }
    os << " --------------\n";

    os << "----- BIDS (best bid at top) -----\n";
    book.for_each_bid_level(
        [&](Price p, const PriceLevel& lvl) {
            os << "  " << std::setw(10) << p
               << "  qty=" << std::setw(8) << lvl.total_quantity()
               << "  orders=" << lvl.order_count() << '\n';
        },
        depth);
}

} // namespace lob
