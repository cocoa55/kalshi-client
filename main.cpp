
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

    return 0;
}
