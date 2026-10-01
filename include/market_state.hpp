#pragma once
#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include "kalshi_messages.hpp"

using Price = int64_t;    // cents, 1..99
using Quantity = int64_t; // hundredths of a contract (Kalshi sizes have 2 decimal places)

inline constexpr Price kMaxPriceCents = 100;

// Kalshi prices are whole cents in [1, 99], so each side of the book is a flat array indexed by price
// rather than a hash map: updates are a single indexed add, there is no hashing or allocation, and the
// whole book (2 x 101 x 8 bytes, ~1.6 KB) stays in L1 cache. The best bid on each side is maintained
// incrementally, so top-of-book reads are O(1); only removing the best level triggers a short downward scan.
class MarketState {
public:
    using Levels = std::array<Quantity, kMaxPriceCents + 1>;

private:
    std::string _market_ticker;
    Levels _yes{};
    Levels _no{};
    Price _best_yes{0}; // 0 means the side is empty (0 is never a valid bid)
    Price _best_no{0};

public:
    // Both return an error for malformed or out-of-range levels. A delta that would drive a level negative
    // means our book has diverged from the exchange's; the level is clamped to zero and an error returned
    // so the caller can resubscribe for a fresh snapshot.
    std::expected<void, std::string> apply_snapshot(const OrderBookSnapshot &snapshot);
    std::expected<void, std::string> apply_delta(const OrderBookDelta &delta);

    const std::string &ticker() const { return _market_ticker; }
    const Levels &yes() const { return _yes; }
    const Levels &no() const { return _no; }

    std::optional<Price> best_yes_bid() const {
        return _best_yes ? std::optional{_best_yes} : std::nullopt;
    }
    // Kalshi books only contain bids. A NO bid at p is equivalent to a YES ask at 100 - p.
    std::optional<Price> best_yes_ask() const {
        return _best_no ? std::optional{kMaxPriceCents - _best_no} : std::nullopt;
    }
};
