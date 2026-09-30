
#include <cstdio>
#include <print>
#include <iostream>

#include "http_client.hpp"
#include "include/web_socket.hpp"
#include "json_lexer.hpp"
#include "json_parser.hpp"
#include "kalshi_auth.hpp"
#include "kalshi_messages.hpp"
#include "market_state.hpp"
#include "net_constants.hpp"
#include "order_tracker.hpp"
#include "position_tracker.hpp"
#include "time_util.hpp"

int main() {
    WebSocket ws;
    std::println("Initializing connection to Kalshi...");

    auto result = ws.connect("external-api-ws.demo.kalshi.co", "443");

    if (!result.has_value()) {
        std::println(stderr, "Fatal error: {}", result.error());
        return 1;
    }
    std::println("WebSocket connection established.");

    auto frame_received = ws.receive_frame();
    if (!frame_received.has_value()) {
        std::println(stderr, "Failed to receive frame: {}", frame_received.error());
        return 1;
    }

    std::println("Received frame - opcode: {}, payload size: {}", static_cast<uint8_t>(frame_received->op_code),
                 frame_received->payload.size());

    if (frame_received->op_code == WebSocketFrame::Opcode::Close) {
        // echo the close frame back
        WebSocketFrame close_response{.fin_bit = true,
                                      .op_code = WebSocketFrame::Opcode::Close,
                                      .mask_key = std::nullopt,
                                      .payload = frame_received->payload};
        auto close_result = ws.send_frame(close_response);

        if (!close_result.has_value()) {
            std::println(stderr, "Failed to send close frame: {}", close_result.error());
        }

        std::println("Connection closed by server.");
        return 0;
    }
    if (frame_received->op_code == WebSocketFrame::Opcode::Ping) { // we received a ping
        auto pong_result = ws.send_pong(frame_received.value());
        if (!pong_result.has_value()) {
            std::println(stderr, "Failed to send frame: {}", pong_result.error());
            return 1;
        }
        std::println("Sent Pong.");
    }

    std::string subscribe_msg = R"({
    "id": 1,
    "cmd": "subscribe",
    "params": {
        "channels": ["orderbook_delta"],
        "market_ticker": "KXPRESNOMD-28-LC"
    }
})";

    auto *begin = reinterpret_cast<const std::byte *>(subscribe_msg.data());
    std::vector<std::byte> payload(begin, begin + subscribe_msg.size());


    WebSocketFrame sub_frame{.fin_bit = true,
                             .op_code = WebSocketFrame::Opcode::Text,
                             .mask_key = std::nullopt,
                             .payload = std::move(payload)};

    auto sub_result = ws.send_frame(sub_frame);
    if (!sub_result.has_value()) {
        std::println(stderr, "Failed to send subscribe: {}", sub_result.error());
        return 1;
    }
    std::println("Sent subscribe message.");

    MarketState state;
    OrderTracker order_tracker;
    PositionTracker position_tracker;

    for (int i{}; i < 2; ++i) {
        auto data_frame = ws.receive_frame();
        if (!data_frame.has_value()) {
            std::println(stderr, "Failed to receive: {}", data_frame.error());
            return 1;
        }
        if (data_frame->op_code == WebSocketFrame::Opcode::Ping) {
            auto pong_result = ws.send_pong(data_frame.value());
            if (!pong_result.has_value()) {
                std::println("Failed to send pong.");
            }
            --i;
            continue;
        }
        if (data_frame->op_code == WebSocketFrame::Opcode::Text) {
            std::string json_str(reinterpret_cast<const char *>(data_frame->payload.data()), data_frame->payload.size());
            std::println("JSON: {}", json_str);
            auto tokens = json_lexer((data_frame->payload));
            if (!tokens.has_value()) {
                std::println(stderr, "Lexer error: {}", tokens.error());
                continue;
            }
            auto json = json_parser(*tokens);
            if (!json.has_value()) {
                std::println(stderr, "Parser error: {}", json.error());
                continue;
            }
            auto message = parse_kalshi_message(*json);
            if (!message.has_value()) {
                std::println(stderr, "Deserializer error: {}", message.error());
                continue;
            }
            if (message->type == "orderbook_snapshot") {
                auto& snapshot = std::get<OrderBookSnapshot>(message->msg);
                state.apply_snapshot(snapshot);
                std::println("Snapshot applied. Yes levels: {}, No levels: {}",
                    state.yes().size(), state.no().size());
            }
            else if (message->type == "orderbook_delta") {
                auto& delta = std::get<OrderBookDelta>(message->msg);
                state.apply_delta(delta);
                std::println("Delta applied. Yes levels: {}, No levels: {}",
                    state.yes().size(), state.no().size());
            }
            else if (message->type == "fill") {
                auto& fill = std::get<Fill>(message->msg);
                order_tracker.update_status(fill.order_id, OrderStatus::Filled);
                int64_t quantity = static_cast<int64_t>(std::stod(fill.count_fp));
                if (fill.action == FillAction::Buy) {
                    position_tracker.update_positions(fill.market_ticker, quantity);
                } else {
                    position_tracker.update_positions(fill.market_ticker, -quantity);
                }
                std::println("Fill received. Order: {}, Position in {}: {}",
                fill.order_id,
                fill.market_ticker,
                position_tracker.get_position(fill.market_ticker));
            }


            std::println("Parsed message type: {}", message->type);
        }
        std::println("Received frame - opcode: {}, payload size: {}", static_cast<uint8_t>(data_frame->op_code),
                     data_frame->payload.size());
    }
    std::println("Loop exited.");
    
    auto credentials = kalshi_auth::load_credentials_from_env();
    if (!credentials.has_value()) {
        std::println(stderr, "Failed to load credentials: {}", credentials.error());
        return 1;
    }
    OrderRequest order {
        .ticker =  "KXPRESNOMD-28-LC",
        .side = OrderSide::Bid,
        .count = "1.00",
        .price = "0.10",
        .time_in_force = TimeInForce::GoodTillCanceled,
        .self_trade_prevention = SelfTradePrevention::TakerAtCross,
        .client_order_id = "test-order-009",
        .post_only = false
    };

    std::string body = serialize_order_request(order);
    auto response = http_post(kKalshiHost, kOrdersPath, body, *credentials);
    if (!response.has_value()) {
        std::println(stderr, "Order failed: {}", response.error());
        return 1;
    }
    std::println("Order submitted successfully.");

    auto parsed = parse_order_response(*response);
    if (!parsed.has_value()) {
        std::println(stderr, "Failed to parse response: {}", parsed.error());
        return 1;
    }
    std::println("Order ID: {}", parsed->order_id);
    std::println("Fill count: {}", parsed->fill_count);
    std::println("Remaining: {}", parsed->remaining_count);

    return 0;
}
