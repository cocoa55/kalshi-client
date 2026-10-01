#include "trading_bot.hpp"

#include <csignal>
#include <cstdio>
#include <format>
#include <functional>
#include <print>
#include <ranges>
#include <thread>

#include "fixed_point.hpp"
#include "net_constants.hpp"

namespace {
    constexpr std::chrono::seconds kMaxReconnectBackoff{60};
    constexpr std::chrono::seconds kStatusInterval{30};
    // Ping the REST connection when idle so the order path never pays for a new handshake.
    constexpr std::chrono::seconds kHttpKeepWarmAfter{20};

    volatile std::sig_atomic_t g_running = 1;

    // The bot only places whole-contract orders, so fills are whole contracts ("1.00").
    std::optional<Quantity> parse_contracts(const std::string &count_fp) { return parse_fixed<0>(count_fp); }

    std::string price_str(const std::optional<Price> p) { return p.transform(format_price).value_or("-"); }

    // Fair value is a fractional number of ticks; show it in cents like everything else.
    std::string fair_str(const std::optional<double> fair) {
        return fair.transform([](const double ticks) { return std::format("{:.1f}c", ticks / kTicksPerCent); }).value_or("-");
    }

    bool is_book_message(const Message &m) {
        return std::holds_alternative<OrderBookSnapshot>(m.msg) || std::holds_alternative<OrderBookDelta>(m.msg);
    }
} // namespace

TradingBot::TradingBot(std::string ticker, const kalshi_auth::Signer &signer, const StrategyConfig &config)
    : _ticker(std::move(ticker)), _signer(signer), _strategy(config), _http(std::string{kKalshiHost}, &signer) {}

void TradingBot::request_stop() { g_running = 0; }

Quantity TradingBot::exposure() const {
    return std::ranges::fold_left(_unconfirmed_fills | std::views::values, _positions.get_position(_ticker), std::plus{});
}

void TradingBot::submit(const Signal &signal, const Clock::time_point tick_time) {
    const auto client_id = std::format("bot-{}-{}", current_timestamp_ms(), ++_order_seq);
    const auto request = make_ioc_order(_ticker, signal, client_id);

    const auto sent_at = Clock::now();
    auto response = _http.request_json("POST", kOrdersPath, serialize_order_request(request));
    const auto done_at = Clock::now();
    _tick_to_order.record(_http.last_send_time() - tick_time);
    _order_round_trip.record(done_at - sent_at);

    std::println("[order] {} {} @ {} (fair {}, exposure {})", signal.side == OrderSide::Bid ? "BUY" : "SELL",
                 signal.count, format_price(signal.price), fair_str(_strategy.fair_value()), exposure());
    if (!response) {
        std::println(stderr, "[order] request failed: {}", response.error());
        return;
    }
    auto parsed = parse_order_response(*response);
    if (!parsed) {
        std::println(stderr, "[order] rejected or unparseable response: {}", parsed.error());
        return;
    }

    const auto filled = parse_contracts(parsed->fill_count);
    if (!filled) {
        std::println(stderr, "[order] unexpected fill_count '{}'", parsed->fill_count);
        return;
    }
    _orders.add_order(Order{.order_id = parsed->order_id,
                            .order_request = request,
                            // IOC: whatever didn't fill immediately is cancelled by the exchange.
                            .order_status = *filled > 0 ? OrderStatus::Filled : OrderStatus::Cancelled,
                            .fill_count = parsed->fill_count,
                            .remaining_count = parsed->remaining_count});
    if (*filled > 0)
        _unconfirmed_fills[parsed->order_id] += signal.side == OrderSide::Bid ? *filled : -*filled;
    std::println("[order] {} filled {}, remaining {} ({:.1f} ms)", parsed->order_id, parsed->fill_count,
                 parsed->remaining_count, std::chrono::duration<double, std::milli>(done_at - sent_at).count());
}

void TradingBot::on_fill(const Fill &fill) {
    const auto qty = parse_contracts(fill.count_fp);
    if (!qty) {
        std::println(stderr, "[fill] unexpected count '{}' for order {}", fill.count_fp, fill.order_id);
        return;
    }
    const Quantity signed_qty = fill.action == FillAction::Buy ? *qty : -*qty;
    _orders.update_status(fill.order_id, OrderStatus::Filled);
    _positions.update_positions(fill.market_ticker, signed_qty);

    if (auto it = _unconfirmed_fills.find(fill.order_id); it != _unconfirmed_fills.end()) {
        const Quantity before = it->second;
        it->second -= signed_qty;
        if (it->second == 0 || (before > 0) != (it->second > 0))
            _unconfirmed_fills.erase(it);
    }
    std::println("[fill] {} {} {} -> position {}", fill.order_id, fill.action == FillAction::Buy ? "+" : "-", *qty,
                 _positions.get_position(fill.market_ticker));
}

// Out-of-sync books are fixed by resubscribing (the caller drops the session). Malformed data won't be fixed by
// a fresh snapshot, and resubscribing would just loop forever, so stop instead.
void TradingBot::on_book_error(const BookError &error) {
    if (error.kind == BookError::Kind::OutOfSync) {
        std::println(stderr, "[book] {}; resubscribing", error.message);
        return;
    }
    std::println(stderr, "[book] {}; stopping (resubscribing would get the same data)", error.message);
    request_stop();
}

void TradingBot::keep_http_warm() {
    if (_http.connected() && _http.idle_for() < kHttpKeepWarmAfter)
        return;
    if (auto r = _http.request("GET", kExchangeStatusPath); !r)
        std::println(stderr, "[http] keep-warm request failed: {}", r.error());
}

void TradingBot::print_status() const {
    std::println("[status] bid {} ask {} fair {} position {} exposure {} | http connections opened: {}",
                 price_str(_book.best_yes_bid()), price_str(_book.best_yes_ask()),
                 fair_str(_strategy.fair_value()), _positions.get_position(_ticker), exposure(),
                 _http.connections_opened());
}

void TradingBot::print_latency_summary() const {
    std::println("Latency (most recent samples):");
    for (const auto *stats: {&_decode, &_book_and_strategy, &_tick_to_order, &_order_round_trip})
        std::println("  {}", stats->summary());
}

bool TradingBot::run_session() {
    WebSocket ws;
    if (auto result = ws.connect(std::string{kKalshiWsHost}, "443", _signer); !result) {
        std::println(stderr, "[ws] connect failed: {}", result.error());
        return false;
    }
    const std::string subscribe_msg = std::format(
            R"({{"id":1,"cmd":"subscribe","params":{{"channels":["orderbook_delta","fill"],"market_ticker":"{}"}}}})",
            _ticker);
    if (auto result = ws.send_text(subscribe_msg); !result) {
        std::println(stderr, "[ws] subscribe failed: {}", result.error());
        return false;
    }
    std::println("[ws] subscribed to {}", _ticker);

    _book = MarketState{}; // rebuilt from the snapshot that follows the subscription
    _book_sid.reset();
    bool received_data = false;
    auto last_status = Clock::now();

    while (g_running) {
        auto frame = ws.receive_frame();
        const auto t_rx = Clock::now();
        if (!frame) {
            if (g_running)
                std::println(stderr, "[ws] receive failed: {}", frame.error());
            return received_data;
        }
        if (frame->op_code == WebSocketFrame::Opcode::Ping) {
            if (auto pong = ws.send_pong(*frame); !pong)
                std::println(stderr, "[ws] failed to send pong: {}", pong.error());
        } else if (frame->op_code == WebSocketFrame::Opcode::Close) {
            std::println("[ws] server closed the connection");
            return received_data;
        } else if (frame->op_code == WebSocketFrame::Opcode::Text) {
            auto message = decode_kalshi_message(frame->payload);
            if (!message) {
                std::println(stderr, "[ws] {}", message.error());
                continue;
            }
            const auto t_decoded = Clock::now();
            _decode.record(t_decoded - t_rx);

            // Deltas are only meaningful applied in order on top of the snapshot. A gap in seq means we missed
            // one and the book is wrong, so drop the session and resubscribe for a fresh snapshot.
            // seq == 0 means the field was absent, so there's nothing to check.
            if (is_book_message(*message) && message->seq != 0) {
                const bool is_snapshot = std::holds_alternative<OrderBookSnapshot>(message->msg);
                if (!is_snapshot && (!_book_sid || message->sid != *_book_sid || message->seq != _book_seq + 1)) {
                    std::println(stderr, "[ws] orderbook sequence gap (expected {}, got {}); resubscribing",
                                 _book_seq + 1, message->seq);
                    return received_data;
                }
                _book_sid = message->sid;
                _book_seq = message->seq;
            } else if (_book_sid && message->sid == *_book_sid && message->seq != 0) {
                _book_seq = message->seq; // another channel sharing the subscription's sequence
            }

            bool book_changed = false;
            if (auto *snapshot = std::get_if<OrderBookSnapshot>(&message->msg)) {
                if (auto r = _book.apply_snapshot(*snapshot); !r) {
                    on_book_error(r.error());
                    return received_data;
                }
                book_changed = received_data = true;
            } else if (auto *delta = std::get_if<OrderBookDelta>(&message->msg)) {
                if (auto r = _book.apply_delta(*delta); !r) {
                    on_book_error(r.error());
                    return received_data;
                }
                book_changed = true;
            } else if (auto *fill = std::get_if<Fill>(&message->msg)) {
                on_fill(*fill);
            }

            if (book_changed) {
                auto signal = _strategy.on_book_update(_book, exposure(), Clock::now());
                _book_and_strategy.record(Clock::now() - t_decoded);
                if (signal)
                    submit(*signal, t_rx);
            }
        }

        // Housekeeping runs after the latency-sensitive work for this frame is done.
        keep_http_warm();
        if (const auto now = Clock::now(); now - last_status >= kStatusInterval) {
            print_status();
            last_status = now;
        }
    }
    return received_data;
}

void TradingBot::run() {
    const auto &cfg = _strategy.config();
    std::println("Trading {} | edge {}, max spread {}, max position {}, cooldown {}s", _ticker,
                 format_price(cfg.entry_edge), format_price(cfg.max_spread), cfg.max_position, cfg.cooldown.count());

    if (auto r = _http.connect(); !r)
        std::println(stderr, "[http] could not pre-open order connection: {}", r.error());

    // Reconnect forever with exponential backoff; reset backoff once a session actually streams data.
    std::chrono::seconds backoff{1};
    while (g_running) {
        if (run_session())
            backoff = std::chrono::seconds{1};
        if (!g_running)
            break;

        std::println("[ws] reconnecting in {}s", backoff.count());
        for (auto waited = std::chrono::seconds{0}; waited < backoff && g_running; ++waited)
            std::this_thread::sleep_for(std::chrono::seconds{1});
        backoff = std::min(backoff * 2, kMaxReconnectBackoff);
    }
}
