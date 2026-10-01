#pragma once
#include <expected>
#include <memory>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <span>
#include <string>
#include <vector>

#include "tcp_socket.hpp"

class TlsSocket {
private:
    struct SslCtxDeleter {
        static void operator()(SSL_CTX* ctx) noexcept { SSL_CTX_free(ctx); }
    };
    struct SslDeleter {
        static void operator()(SSL* ssl) noexcept { SSL_shutdown(ssl); SSL_free(ssl); }
    };

    TcpSocket _tcp_socket {};
    std::unique_ptr<SSL_CTX, SslCtxDeleter> _ctx;
    std::unique_ptr<SSL, SslDeleter> _ssl;

public:
    TlsSocket();

    [[nodiscard]] std::expected<void, std::string> connect(const std::string& host, const std::string& port);

    int fd() const { return _tcp_socket.fd(); }

    [[nodiscard]] std::expected<ssize_t, std::string> send_data(std::span<const std::byte> data) const;
    [[nodiscard]] std::expected<std::vector<std::byte>,std::string>receive_data() const;
};
