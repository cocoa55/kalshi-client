#include "tcp_socket.hpp"
#include "net_constants.hpp"
#include <array>
#include <cstring>
#include <memory>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
    // Non-blocking connect + poll gives connect() a deadline, then restores blocking mode.
    bool connect_with_timeout(const int fd, const sockaddr *addr, const socklen_t len) {
        const int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
            return false;

        if (::connect(fd, addr, len) == -1) {
            if (errno != EINPROGRESS)
                return false;
            pollfd pfd{.fd = fd, .events = POLLOUT, .revents = 0};
            if (poll(&pfd, 1, kConnectTimeoutMs) != 1)
                return false; // timed out or poll error
            int so_error = 0;
            socklen_t so_len = sizeof(so_error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_len) == -1 || so_error != 0)
                return false;
        }
        return fcntl(fd, F_SETFL, flags) != -1;
    }
} // namespace

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
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res_raw {nullptr};

    int result {getaddrinfo(host.c_str(), port.c_str(), &hints, &res_raw)};

    if (result != 0) {
        return std::unexpected(gai_strerror(result));
    }

    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> res(res_raw, freeaddrinfo);

    for (auto* p = res.get(); p != nullptr; p = p->ai_next) {
        _fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (_fd == -1) continue;

        if (!connect_with_timeout(_fd, p->ai_addr, p->ai_addrlen)) {
            close(_fd);
            _fd = -1;
            continue;
        }
        // Orders and pongs are small writes that must go out immediately; don't let Nagle's
        // algorithm hold them back waiting for an ACK.
        const int one = 1;
        setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
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
