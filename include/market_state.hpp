#pragma once
#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include "kalshi_messages.hpp"

// Prices are integers in tenths of a cent ($0.001). Most Kalshi markets tick in whole cents, but some use
// deci-cent ($0.001) ticks across the whole range or near $0 and $1 ("deci_cent" / "tapered_deci_cent"),
// so the book is kept at the finest resolution Kalshi uses.
using Price = int64_t;
using Quantity = int64_t; // hundredths of a contract (Kalshi sizes have 2 decimal places)

inline constexpr int kPriceDecimals = 3;   // digits after the dollar point that Price represents
inline constexpr Price kTicksPerCent = 10;
inline constexpr Price kMaxPrice = 1000;   // $1.00

constexpr Price cents(const int64_t c) { return c * kTicksPerCent; }

// "59c" for whole cents, "0.1c" / "59.5c" for deci-cent prices.
inline std::string format_price(const Price p) {
    if (p % kTicksPerCent == 0)
        return std::format("{}c", p / kTicksPerCent);
    return std::format("{}.{}c", p / kTicksPerCent, p % kTicksPerCent);
}

// Kalshi's wire format: fixed-point dollars with 4 decimal places, e.g. 595 -> "0.5950".
inline std::string price_to_dollars(const Price p) { return std::format("{}.{:03}0", p / kMaxPrice, p % kMaxPrice); }

struct BookError {
    enum class Kind {
        Malformed, // data the book can't represent (bad number, unsupported precision): resubscribing won't help
        OutOfSync, // a level went negative, so our book diverged from the exchange's: resubscribe for a snapshot
    };
    Kind kind;
    std::string message;
};

// Each side of the book is a flat array indexed by price rather than a hash map: updates are a single indexed
// add with no hashing or allocation, and the whole book (2 x 1001 x 8 bytes, ~16 KB) fits in L1 cache. The best
// bid on each side is maintained incrementally, so top-of-book reads are O(1); only removing the best level
// triggers a downward scan over contiguous memory.
class MarketState {
public:
    using Levels = std::array<Quantity, kMaxPrice + 1>;

private:
    std::string _market_ticker;
    Levels _yes{};
    Levels _no{};
    Price _best_yes{0}; // 0 means the side is empty (0 is never a valid bid)
    Price _best_no{0};

public:
    // A malformed snapshot leaves the current book untouched. A delta that would drive a level negative is
    // clamped to zero and reported as OutOfSync.
    std::expected<void, BookError> apply_snapshot(const OrderBookSnapshot &snapshot);
    std::expected<void, BookError> apply_delta(const OrderBookDelta &delta);

    const std::string &ticker() const { return _market_ticker; }
    const Levels &yes() const { return _yes; }
    const Levels &no() const { return _no; }

    std::optional<Price> best_yes_bid() const {
        return _best_yes ? std::optional{_best_yes} : std::nullopt;
    }
    // Kalshi books only contain bids. A NO bid at p is equivalent to a YES ask at $1 - p.
    std::optional<Price> best_yes_ask() const {
        return _best_no ? std::optional{kMaxPrice - _best_no} : std::nullopt;
    }
};
