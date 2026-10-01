#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include "kalshi_messages.hpp"

using Price = int64_t;
using Quantity = int64_t;
using OrderBook = std::unordered_map<Price, Quantity>;

inline constexpr Price kMaxPriceCents = 100;

class MarketState {
    std::string _market_ticker;
    OrderBook _yes_levels;
    OrderBook _no_levels;
public:
    void apply_snapshot(const OrderBookSnapshot& snapshot);
    void apply_delta(const OrderBookDelta& delta);
    const std::string& ticker() const {return _market_ticker;}
    const OrderBook& yes() const {return _yes_levels;}
    const OrderBook& no() const {return _no_levels;}
    std::optional<Price> best_yes_bid() const;
    std::optional<Price> best_yes_ask() const;
};