
#include "include/web_socket.hpp"
#include <iostream>

int main() {
    WebSocket ws;
    std::cout << "Initializing connection to Kalshi... \n";

    auto result = ws.connect("api.kalshi.com", "80");

    if (!result.has_value()) {
        std::cerr << "Fatal error: " << result.error() << '\n';
        return 1;
    }
    std::cout << "WebSocket connection established.\n";

return 0;
}