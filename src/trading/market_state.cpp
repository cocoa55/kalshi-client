#include "market_state.hpp"

#include <format>

#include "fixed_point.hpp"

namespace {
    std::unexpected<BookError> malformed(std::string message) {
        return std::unexpected(BookError{.kind = BookError::Kind::Malformed, .message = std::move(message)});
    }

    std::expected<Price, BookError> parse_price(const std::string &s) {
        const auto price = parse_fixed<kPriceDecimals>(s);
        if (!price || *price < 1 || *price >= kMaxPrice)
            return malformed(std::format("unsupported price '{}' (book resolution is $0.001)", s));
        return *price;
    }

    std::expected<Quantity, BookError> parse_quantity(const std::string &s) {
        const auto qty = parse_fixed<2>(s);
        if (!qty)
            return malformed(std::format("invalid quantity '{}'", s));
        return *qty;
    }

    Price scan_best(const MarketState::Levels &levels, Price from) {
        for (Price p = from; p > 0; --p)
            if (levels[p] > 0)
                return p;
        return 0;
    }
} // namespace

std::expected<void, BookError> MarketState::apply_snapshot(const OrderBookSnapshot &snapshot) {
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
    _best_yes = scan_best(_yes, kMaxPrice - 1);
    _best_no = scan_best(_no, kMaxPrice - 1);
    return {};
}

std::expected<void, BookError> MarketState::apply_delta(const OrderBookDelta &delta) {
    const bool is_yes = delta.side == "yes";
    if (!is_yes && delta.side != "no")
        return malformed(std::format("invalid side '{}'", delta.side));
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

    std::expected<void, BookError> result{};
    if (level < 0) {
        result = std::unexpected(BookError{
                .kind = BookError::Kind::OutOfSync,
                .message = std::format("book out of sync: {} level {} went negative", delta.side, format_price(*price))});
        level = 0;
    }

    if (level > 0 && *price > best)
        best = *price;
    else if (level == 0 && *price == best)
        best = scan_best(levels, *price - 1);
    return result;
}
