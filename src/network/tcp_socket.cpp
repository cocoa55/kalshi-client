#include "tcp_socket.hpp"
#include "net_constants.hpp"
#include <array>
#include <cstring>
#include <memory>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

TcpSocket::~TcpSocket() {
    if (_fd != -1)
        close(_fd);
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        if (_fd != -1)
            close(_fd);
        _fd = std::exchange(other._fd, -1);
    }
    return *this;
}

std::expected<int, std::string> TcpSocket::connect(const std::string& host, const std::string& port) {
    addrinfo hints{.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
    addrinfo* res_raw {nullptr};

    int result {getaddrinfo(host.c_str(), port.c_str(), &hints, &res_raw)};

    if (result != 0) {
        return std::unexpected(gai_strerror(result));
    }

    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> res(res_raw, freeaddrinfo);

    for (auto* p = res.get(); p != nullptr; p = p->ai_next) {
        _fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (_fd == -1) continue;

        if (::connect(_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(_fd);
            _fd = -1;
            continue;
        }
        // Without a timeout a silently dropped connection blocks recv/SSL_read forever.
        // Kalshi pings every ~10s, so 30s of silence means the connection is dead.
        timeval timeout{.tv_sec = kReceiveTimeoutSeconds, .tv_usec = 0};
        setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        return _fd;
    }
    return std::unexpected("Failed to connect to any resolved address");
}

std::expected<ssize_t, std::string> TcpSocket::send_data(const std::string& request) const {
    ssize_t bytes = ::send(_fd, request.c_str(), request.length(), 0);

    if (bytes == -1) {
        return std::unexpected(strerror(errno));
    }

    return bytes;
}

std::expected<std::string, std::string>  TcpSocket::receive_data() const {

    std::array<char, kReceiveBufferSize> buffer{};

    ssize_t bytes = ::recv(_fd, buffer.data(), buffer.size(), 0);
    if (bytes == -1)
        return std::unexpected(strerror(errno));
    if (bytes == 0)
        return std::unexpected("Connection closed by server.");

    return std::string{buffer.data(), static_cast<size_t>(bytes)};
}
