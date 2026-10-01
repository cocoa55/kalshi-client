#pragma once

#include <string>
#include <expected>
#include <sys/types.h>
#include <utility>

class TcpSocket {
private:
    int _fd = -1;

public:
    TcpSocket() = default;
    ~TcpSocket();
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept : _fd(std::exchange(other._fd, -1)) {}
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    // Connects with a bounded timeout (a blackholed SYN would otherwise block for minutes), then
    // enables TCP_NODELAY and a receive timeout.
    [[nodiscard]] std::expected<int, std::string> connect (const std::string& host, const std::string& port);
    int fd() const { return _fd; }

    [[nodiscard]] std::expected<ssize_t, std::string> send_data(const std::string& request) const;
    [[nodiscard]] std::expected<std::string, std::string> receive_data() const;

};