#pragma once

#include "lob/OrderBook.hpp"

#include <ostream>

namespace lob {

// Renders the top `depth` levels on each side in the conventional
// trading-screen layout (asks above, bids below, best closest to the
// spread). Kept separate from OrderBook so the core has no iostream
// dependency.
void print_book(const OrderBook& book, std::ostream& os, std::size_t depth = 10);

} // namespace lob
