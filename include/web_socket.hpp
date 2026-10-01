#pragma once
#include "kalshi_auth.hpp"
#include "tls_socket.hpp"
#include "web_socket_frame.hpp"

class WebSocket {
    TlsSocket _tls_socket {};
    std::vector<std::byte> _buffer {};
public:
    std::expected<void, std::string> connect(const std::string &host, const std::string &port,
                                             const kalshi_auth::Signer &signer);
    std::expected<WebSocketFrame, std::string> receive_frame();
    std::expected<void, std::string> send_frame(const WebSocketFrame& frame);

    std::expected<void, std::string> send_pong(const WebSocketFrame& ping_frame);
    std::expected<void, std::string> send_text(std::string_view text);
};