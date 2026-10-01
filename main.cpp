#include <cmath>
#include <csignal>
#include <cstdio>
#include <format>
#include <print>
#include <thread>

#include "http_client.hpp"
#include "json_lexer.hpp"
#include "json_parser.hpp"
#include "kalshi_auth.hpp"
#include "kalshi_messages.hpp"
#include "market_state.hpp"
#include "net_constants.hpp"
#include "order_tracker.hpp"
#include "position_tracker.hpp"
#include "strategy.hpp"
#include "time_util.hpp"
#include "web_socket.hpp"

namespace {

constexpr std::string_view kDefaultTicker = "KXPRESNOMD-28-LC";
constexpr std::chrono::seconds kMaxReconnectBackoff{60};
constexpr std::chrono::seconds kStatusInterval{30};

volatile std::sig_atomic_t g_running = 1;

void handle_signal(int) { g_running = 0; }

// Installed without SA_RESTART so a blocking SSL_read returns EINTR and the loop can exit promptly.
void install_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

Quantity parse_contracts(const std::string &count_fp) { return std::llround(std::stod(count_fp)); }

std::string price_str(const std::optional<Price> p) { return p ? std::format("{}c", *p) : "-"; }

struct Bot {
    std::string ticker;
    kalshi_auth::Credentials credentials;
    MeanReversionStrategy strategy;
    OrderTracker orders;
    PositionTracker positions;
    // Fills reported by the REST response but not yet seen on the WebSocket fill channel.
    // Counting these in exposure closes the window where a second order could breach the position limit.
    std::unordered_map<OrderId, Quantity> unconfirmed_fills;
    uint64_t order_seq{};

    Quantity exposure() const {
        Quantity total = positions.get_position(ticker);
        for (const auto &[id, qty]: unconfirmed_fills)
            total += qty;
        return total;
    }

    void submit(const Signal &signal) {
        const auto client_id = std::format("bot-{}-{}", current_timestamp_ms(), ++order_seq);
        const auto request = make_ioc_order(ticker, signal, client_id);
        std::println("[order] {} {} @ {}c (fair {:.1f}, exposure {})", signal.side == OrderSide::Bid ? "BUY" : "SELL",
                     signal.count, signal.price, strategy.fair_value().value_or(0), exposure());

        auto response = http_post(kKalshiHost, kOrdersPath, serialize_order_request(request), credentials);
        if (!response) {
            std::println(stderr, "[order] request failed: {}", response.error());
            return;
        }
        auto parsed = parse_order_response(*response);
        if (!parsed) {
            std::println(stderr, "[order] rejected or unparseable response: {}", parsed.error());
            return;
        }

        const Quantity filled = parse_contracts(parsed->fill_count);
        orders.add_order(Order{.order_id = parsed->order_id,
                               .order_request = request,
                               // IOC: whatever didn't fill immediately is cancelled by the exchange.
                               .order_status = filled > 0 ? OrderStatus::Filled : OrderStatus::Cancelled,
                               .fill_count = parsed->fill_count,
                               .remaining_count = parsed->remaining_count});
        if (filled > 0)
            unconfirmed_fills[parsed->order_id] += signal.side == OrderSide::Bid ? filled : -filled;
        std::println("[order] {} filled {}, remaining {}", parsed->order_id, parsed->fill_count,
                     parsed->remaining_count);
    }

    void on_fill(const Fill &fill) {
        const Quantity qty = parse_contracts(fill.count_fp);
        const Quantity signed_qty = fill.action == FillAction::Buy ? qty : -qty;
        orders.update_status(fill.order_id, OrderStatus::Filled);
        positions.update_positions(fill.market_ticker, signed_qty);

        if (auto it = unconfirmed_fills.find(fill.order_id); it != unconfirmed_fills.end()) {
            const Quantity before = it->second;
            it->second -= signed_qty;
            if (it->second == 0 || (before > 0) != (it->second > 0))
                unconfirmed_fills.erase(it);
        }
        std::println("[fill] {} {} {} -> position {}", fill.order_id, fill.action == FillAction::Buy ? "+" : "-", qty,
                     positions.get_position(fill.market_ticker));
    }

    // Runs one WebSocket session until it drops. Returns whether the session got far enough to receive data,
    // which the caller uses to reset reconnect backoff.
    bool run_session() {
        WebSocket ws;
        if (auto result = ws.connect(std::string{kKalshiWsHost}, "443"); !result) {
            std::println(stderr, "[ws] connect failed: {}", result.error());
            return false;
        }

        const std::string subscribe_msg = std::format(
                R"({{"id":1,"cmd":"subscribe","params":{{"channels":["orderbook_delta","fill"],"market_ticker":"{}"}}}})",
                ticker);
        auto *begin = reinterpret_cast<const std::byte *>(subscribe_msg.data());
        WebSocketFrame sub_frame{.fin_bit = true,
                                 .op_code = WebSocketFrame::Opcode::Text,
                                 .mask_key = std::nullopt,
                                 .payload = std::vector(begin, begin + subscribe_msg.size())};
        if (auto result = ws.send_frame(sub_frame); !result) {
            std::println(stderr, "[ws] subscribe failed: {}", result.error());
            return false;
        }
        std::println("[ws] subscribed to {}", ticker);

        MarketState book; // fresh per session; the snapshot after subscribing rebuilds it
        bool received_data = false;
        auto last_status = Clock::now();

        while (g_running) {
            auto frame = ws.receive_frame();
            if (!frame) {
                std::println(stderr, "[ws] receive failed: {}", frame.error());
                return received_data;
            }
            if (frame->op_code == WebSocketFrame::Opcode::Ping) {
                if (auto pong = ws.send_pong(*frame); !pong)
                    std::println(stderr, "[ws] failed to send pong: {}", pong.error());
                continue;
            }
            if (frame->op_code == WebSocketFrame::Opcode::Close) {
                std::println("[ws] server closed the connection");
                return received_data;
            }
            if (frame->op_code != WebSocketFrame::Opcode::Text)
                continue;

            auto tokens = json_lexer(frame->payload);
            if (!tokens) {
                std::println(stderr, "[ws] lexer error: {}", tokens.error());
                continue;
            }
            auto json = json_parser(*tokens);
            if (!json) {
                std::println(stderr, "[ws] parser error: {}", json.error());
                continue;
            }
            auto message = parse_kalshi_message(*json);
            if (!message) {
                std::println(stderr, "[ws] {}", message.error());
                continue;
            }

            bool book_changed = false;
            if (auto *snapshot = std::get_if<OrderBookSnapshot>(&message->msg)) {
                book.apply_snapshot(*snapshot);
                book_changed = received_data = true;
            } else if (auto *delta = std::get_if<OrderBookDelta>(&message->msg)) {
                book.apply_delta(*delta);
                book_changed = true;
            } else if (auto *fill = std::get_if<Fill>(&message->msg)) {
                on_fill(*fill);
            }

            const auto now = Clock::now();
            if (book_changed) {
                if (auto signal = strategy.on_book_update(book, exposure(), now))
                    submit(*signal);
            }
            if (now - last_status >= kStatusInterval) {
                std::println("[status] bid {} ask {} fair {:.1f} position {} exposure {}",
                             price_str(book.best_yes_bid()), price_str(book.best_yes_ask()),
                             strategy.fair_value().value_or(0), positions.get_position(ticker), exposure());
                last_status = now;
            }
        }
        return received_data;
    }
};

// One-off admin command. Collateral is per exchange shard (e.g. baseball/tennis/basketball live on shard 3),
// so funds must be moved from the default shard 0 before trading those markets.
int fund_shard(const kalshi_auth::Credentials &credentials, const int shard, const int64_t dollars) {
    auto print_balance = [&] {
        auto balance = http_get(kKalshiHost, kBalancePath, credentials);
        if (balance)
            std::println("Balance: {}", to_json_string(*balance));
        else
            std::println(stderr, "Balance request failed: {}", balance.error());
    };

    print_balance();
    // amount is in centicents: $1 = 10,000
    const std::string body = std::format(
            R"({{"source":"event_contract","destination":"event_contract","amount":{},"source_exchange_shard":0,"destination_exchange_shard":{}}})",
            dollars * 10'000, shard);
    auto response = http_post(kKalshiHost, kShardTransferPath, body, credentials);
    if (!response) {
        std::println(stderr, "Transfer request failed: {}", response.error());
        return 1;
    }
    std::println("Transfer response: {}", to_json_string(*response));
    // Transfers are processed asynchronously, so give it a moment before re-reading.
    std::this_thread::sleep_for(std::chrono::seconds{2});
    print_balance();
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    install_signal_handlers();

    auto credentials = kalshi_auth::load_credentials_from_env();
    if (!credentials) {
        std::println(stderr, "Failed to load credentials: {}", credentials.error());
        return 1;
    }

    if (argc == 4 && std::string_view{argv[1]} == "--fund-shard")
        return fund_shard(*credentials, std::stoi(argv[2]), std::stoll(argv[3]));

    Bot bot{.ticker = argc > 1 ? argv[1] : std::string{kDefaultTicker},
            .credentials = std::move(*credentials),
            .strategy = MeanReversionStrategy{StrategyConfig{}}};

    const auto &cfg = bot.strategy.config();
    std::println("Trading {} | edge {}c, max spread {}c, max position {}, cooldown {}s", bot.ticker,
                 cfg.entry_edge, cfg.max_spread, cfg.max_position, cfg.cooldown.count());

    // Reconnect forever with exponential backoff; reset backoff once a session actually streams data.
    std::chrono::seconds backoff{1};
    while (g_running) {
        if (bot.run_session())
            backoff = std::chrono::seconds{1};
        if (!g_running)
            break;

        std::println("[ws] reconnecting in {}s", backoff.count());
        for (auto waited = std::chrono::seconds{0}; waited < backoff && g_running; ++waited)
            std::this_thread::sleep_for(std::chrono::seconds{1});
        backoff = std::min(backoff * 2, kMaxReconnectBackoff);
    }

    std::println("Shutting down. Final position in {}: {}", bot.ticker, bot.positions.get_position(bot.ticker));
    return 0;
}
