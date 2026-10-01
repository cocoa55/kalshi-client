#include "market_state.hpp"

#include <algorithm>
#include <cmath>

namespace {
    int64_t to_cents(const std::string& s) {
        return std::llround(std::stod(s) * 100);
    }
    std::optional<Price> best_price(const OrderBook& book) {
        if (book.empty())
            return std::nullopt;
        return std::ranges::max_element(book, {}, [](const auto& level) { return level.first; })->first;
    }
} // namespace
void MarketState::apply_snapshot(const OrderBookSnapshot &snapshot) {
    _market_ticker = snapshot.market_ticker;
    _yes_levels.clear();
    _no_levels.clear();
    for (const auto &[price, qty]: snapshot.yes_dollars_fp) {
        _yes_levels[to_cents(price)] = to_cents(qty);
    }
    for (const auto &[price, qty]: snapshot.no_dollars_fp) {
        _no_levels[to_cents(price)] = to_cents(qty);
    }
}
void MarketState::apply_delta(const OrderBookDelta &delta) {
    auto &book = (delta.side == "yes") ? _yes_levels : _no_levels;
    const auto price = to_cents(delta.price_dollars);
    const auto qty = to_cents(delta.delta_fp);
    book[price] += qty;
    if (book[price] <= 0)
        book.erase(price);
}

// Kalshi books only contain bids. A NO bid at p is equivalent to a YES ask at 100 - p.
std::optional<Price> MarketState::best_yes_bid() const {
    return best_price(_yes_levels);
}
std::optional<Price> MarketState::best_yes_ask() const {
    auto best_no_bid = best_price(_no_levels);
    if (!best_no_bid)
        return std::nullopt;
    return kMaxPriceCents - *best_no_bid;
}
