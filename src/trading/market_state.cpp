#include "market_state.hpp"

#include <format>

#include "fixed_point.hpp"

namespace {
    std::expected<Price, std::string> parse_price(const std::string &s) {
        const auto cents = parse_fixed<2>(s);
        if (!cents || *cents < 1 || *cents >= kMaxPriceCents)
            return std::unexpected(std::format("invalid price '{}'", s));
        return *cents;
    }

    std::expected<Quantity, std::string> parse_quantity(const std::string &s) {
        const auto qty = parse_fixed<2>(s);
        if (!qty)
            return std::unexpected(std::format("invalid quantity '{}'", s));
        return *qty;
    }

    Price scan_best(const MarketState::Levels &levels, Price from) {
        for (Price p = from; p > 0; --p)
            if (levels[p] > 0)
                return p;
        return 0;
    }
} // namespace

std::expected<void, std::string> MarketState::apply_snapshot(const OrderBookSnapshot &snapshot) {
    // Parse into scratch arrays first so a malformed snapshot leaves the current book untouched.
    Levels yes{}, no{};
    for (auto [side, levels]: {std::pair{&snapshot.yes_dollars_fp, &yes}, std::pair{&snapshot.no_dollars_fp, &no}}) {
        for (const auto &[price_str, qty_str]: *side) {
            auto price = parse_price(price_str);
            if (!price)
                return std::unexpected(price.error());
            auto qty = parse_quantity(qty_str);
            if (!qty)
                return std::unexpected(qty.error());
            (*levels)[*price] = *qty;
        }
    }
    _market_ticker = snapshot.market_ticker;
    _yes = yes;
    _no = no;
    _best_yes = scan_best(_yes, kMaxPriceCents - 1);
    _best_no = scan_best(_no, kMaxPriceCents - 1);
    return {};
}

std::expected<void, std::string> MarketState::apply_delta(const OrderBookDelta &delta) {
    const bool is_yes = delta.side == "yes";
    if (!is_yes && delta.side != "no")
        return std::unexpected(std::format("invalid side '{}'", delta.side));
    auto price = parse_price(delta.price_dollars);
    if (!price)
        return std::unexpected(price.error());
    auto qty = parse_quantity(delta.delta_fp);
    if (!qty)
        return std::unexpected(qty.error());

    auto &levels = is_yes ? _yes : _no;
    auto &best = is_yes ? _best_yes : _best_no;

    Quantity &level = levels[*price];
    level += *qty;

    std::expected<void, std::string> result{};
    if (level < 0) {
        result = std::unexpected(std::format("book out of sync: {} level {}c went negative", delta.side, *price));
        level = 0;
    }

    if (level > 0 && *price > best)
        best = *price;
    else if (level == 0 && *price == best)
        best = scan_best(levels, *price - 1);
    return result;
}
