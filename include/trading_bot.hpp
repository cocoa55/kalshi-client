#pragma once
#include <optional>
#include <string>
#include <unordered_map>

#include "http_client.hpp"
#include "kalshi_auth.hpp"
#include "latency_stats.hpp"
#include "market_state.hpp"
#include "order_tracker.hpp"
#include "position_tracker.hpp"
#include "strategy.hpp"
#include "web_socket.hpp"

// Single-threaded trading loop for one market: market data in over the WebSocket, orders out over a
// persistent HTTPS connection. Runs until stop() is requested (from a signal handler), reconnecting with
// exponential backoff whenever the WebSocket drops.
class TradingBot {
    std::string _ticker;
    const kalshi_auth::Signer &_signer;
    MeanReversionStrategy _strategy;
    HttpClient _http;
    OrderTracker _orders;
    PositionTracker _positions;
    // Fills reported by the REST response but not yet seen on the WebSocket fill channel.
    // Counting these in exposure closes the window where a second order could breach the position limit.
    std::unordered_map<OrderId, Quantity> _unconfirmed_fills;
    uint64_t _order_seq{0};

    // Per-session order book state.
    MarketState _book;
    std::optional<uint32_t> _book_sid;
    uint32_t _book_seq{0};

    LatencyStats _decode{"decode (frame->msg)"};
    LatencyStats _book_and_strategy{"book + strategy"};
    LatencyStats _tick_to_order{"tick-to-order-sent"};
    LatencyStats _order_round_trip{"order round trip"};

    Quantity exposure() const;
    void submit(const Signal &signal, Clock::time_point tick_time);
    void on_fill(const Fill &fill);
    void on_book_error(const BookError &error);
    void keep_http_warm();
    void print_status() const;
    bool run_session(); // returns whether the session received market data

public:
    TradingBot(std::string ticker, const kalshi_auth::Signer &signer, const StrategyConfig &config);

    void run();
    static void request_stop(); // async-signal-safe
    void print_latency_summary() const;
    Quantity position() const { return _positions.get_position(_ticker); }
    const std::string &ticker() const { return _ticker; }
};
