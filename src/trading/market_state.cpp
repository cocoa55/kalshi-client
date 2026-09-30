#include "market_state.hpp"
namespace {
    int64_t to_cents(const std::string& s) {
        return static_cast<int64_t>(std::stod(s) * 100);
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
