
#include "include/web_socket.hpp"
#include <print>
#include <cstdio>

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
    std::println("Received frame - opcode: {}, payload size: {}",
        static_cast<uint8_t>(frame_received->op_code),
        frame_received->payload.size());

    if (frame_received->op_code == WebSocketFrame::Opcode::Ping) {
        WebSocketFrame Pong {
            .fin_bit = true,
            .op_code = WebSocketFrame::Opcode::Pong,
            .mask_key =  std::nullopt,
            .payload = frame_received->payload
        };

        auto frame_sent = ws.send_frame(Pong); 
        if (!frame_sent.has_value()) {
            std::println(stderr, "Failed to sendframe: {}", frame_sent.error());
            return 1;
        }
        std::println("Sent Frame - opcode: {}, payload size: {}",
            static_cast<uint8_t>(Pong.op_code),
            Pong.payload.size()
        );
    }
    return 0;
}
