#pragma once
// Structural equality for decoded messages, shared by the unit tests and the differential fuzzer.
#include "kalshi_messages.hpp"

inline bool same_payload(const OrderBookSnapshot &a, const OrderBookSnapshot &b) {
    return a.market_ticker == b.market_ticker && a.market_id == b.market_id && a.yes_dollars_fp == b.yes_dollars_fp &&
           a.no_dollars_fp == b.no_dollars_fp;
}
inline bool same_payload(const OrderBookDelta &a, const OrderBookDelta &b) {
    return a.market_ticker == b.market_ticker && a.market_id == b.market_id && a.price_dollars == b.price_dollars &&
           a.delta_fp == b.delta_fp && a.side == b.side && a.ts == b.ts && a.ts_ms == b.ts_ms;
}
inline bool same_payload(const Fill &a, const Fill &b) {
    return a.order_id == b.order_id && a.market_ticker == b.market_ticker && a.count_fp == b.count_fp &&
           a.action == b.action;
}
inline bool same_payload(std::monostate, std::monostate) { return true; }

inline bool same_message(const Message &a, const Message &b) {
    if (a.type != b.type || a.sid != b.sid || a.seq != b.seq || a.msg.index() != b.msg.index())
        return false;
    return std::visit(
            [&](const auto &x) {
                using T = std::decay_t<decltype(x)>;
                return same_payload(x, std::get<T>(b.msg));
            },
            a.msg);
}
