#pragma once
#include "tls_socket.hpp"

class WebSocket {
    TlsSocket _tls_socket {};
public:
    std::expected<void, std::string> connect(const std::string &host, const std::string &port);
};