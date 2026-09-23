
#include <cstdio>
#include <print>

#include "include/web_socket.hpp"
#include "json_lexer.hpp"
#include "json_parser.hpp"
#include "kalshi_messages.hpp"

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
            std::println(stderr, "Failed to sendframe: {}", pong_result.error());
            return 1;
        }
        std::println("Sent Pong.");
    }

    std::string subscribe_msg = R"({
    "id": 1,
    "cmd": "subscribe",
    "params": {
        "channels": ["orderbook_delta"],
        "market_ticker": "KXMLB-26"
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
            std::println("Parsed message type: {}", message->type);
        }
        std::println("Received frame - opcode: {}, payload size: {}", static_cast<uint8_t>(data_frame->op_code),
                     data_frame->payload.size());
    }


    return 0;
}
