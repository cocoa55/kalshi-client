#pragma once
#include "tcp_socket.hpp"

class WebSocket {
    TcpSocket _tcp_socket {};
public:
    std::expected<void, std::string> connect(const std::string &host, const std::string &port);
};