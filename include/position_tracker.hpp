#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

using MarketTicker = std::string;
using Quantity = int64_t;


class PositionTracker {
    std::unordered_map<MarketTicker, Quantity> _positions;

public:
    void update_positions(const MarketTicker& ticker, const Quantity quantity) {
        _positions[ticker] += quantity;
    }
    int64_t get_position(const MarketTicker& ticker) const {
        const auto it = _positions.find(ticker);
        return it != _positions.end() ? it->second : 0;
    }
};