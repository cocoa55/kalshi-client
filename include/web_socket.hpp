#pragma once
#include "tls_socket.hpp"
#include "web_socket_frame.hpp"

class WebSocket {
    TlsSocket _tls_socket {};
public:
    std::expected<void, std::string> connect(const std::string &host, const std::string &port);
    std::expected<WebSocketFrame, std::string> receive_frame();
    std::expected<void, std::string> send_frame(const WebSocketFrame& frame);
};