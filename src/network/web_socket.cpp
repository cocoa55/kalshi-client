#include "web_socket.hpp"
#include <iostream>
#include <format>


std::expected<void, std::string> WebSocket::connect(const std::string &host, const std::string &port) {

    auto tcp_result = _tcp_socket.connect(host,port);
    if (!tcp_result.has_value()) {
        return std::unexpected("TCP connection failed " + tcp_result.error());
    }
    std::string upgrade_request =
            "GET /trade-api/ws/trade/v2 HTTP/1.1\r\n"
            "Host: " + host + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";

    auto send_result = _tcp_socket.send_data(upgrade_request);
    if (!send_result.has_value()) {
        return std::unexpected("Failed to send data " + send_result.error());
    }

    auto receive_result = _tcp_socket.receive_data();

    if (!receive_result.has_value()) {
        return std::unexpected("Failed to receive data " + receive_result.error());
    }
    std::cout << "\n--- Kalshi Server Response ---\n";
    std::cout << receive_result.value() << '\n';
    std::cout << "------------------------------\n";

    return {};
}
