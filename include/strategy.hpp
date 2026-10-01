#pragma once
#include <chrono>
#include <optional>

#include "kalshi_messages.hpp"
#include "market_state.hpp"
#include "time_util.hpp"

struct StrategyConfig {
    std::chrono::seconds ema_time_constant{60}; // how quickly "fair value" adapts
    std::chrono::seconds warmup{60};            // observe this long before the first trade
    Price entry_edge{cents(3)};                 // how far the touch must be from the EMA
    Price max_spread{cents(4)};                 // don't cross spreads wider than this
    Quantity max_position{5};                   // hard cap on |position| in contracts
    Quantity order_size{1};                     // contracts per order
    std::chrono::seconds cooldown{5};           // minimum time between orders
};

struct Signal {
    OrderSide side;
    Price price; // YES price in ticks ($0.001)
    Quantity count;
};

// Mean reversion against a time-decayed EMA of the YES mid price.
// If someone offers YES well below where it has been trading, buy it; if someone bids well above, sell.
// Pure decision logic: no I/O, the caller owns sending orders and tracking positions.
class MeanReversionStrategy {
    StrategyConfig _config;
    std::optional<double> _ema;
    Clock::time_point _first_update{};
    Clock::time_point _last_update{};
    std::optional<Clock::time_point> _last_order;

public:
    explicit MeanReversionStrategy(const StrategyConfig &config) : _config(config) {}

    // `exposure` is the signed YES position including fills not yet confirmed over the WebSocket.
    std::optional<Signal> on_book_update(const MarketState &book, Quantity exposure, Clock::time_point now);

    std::optional<double> fair_value() const { return _ema; }
    const StrategyConfig &config() const { return _config; }
};

// Build an immediate-or-cancel order for a signal. IOC means nothing ever rests on the book,
// so there are no stale orders to cancel if the bot disconnects or the market moves.
OrderRequest make_ioc_order(const std::string &ticker, const Signal &signal, const std::string &client_order_id);
