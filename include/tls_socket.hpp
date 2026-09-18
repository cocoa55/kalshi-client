#pragma once
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <expected>
#include <memory>
#include <string>
#include "tcp_socket.hpp"

class TlsSocket {
private:
    struct SslCtxDeleter {
        void operator()(SSL_CTX* ctx) const noexcept { SSL_CTX_free(ctx); }
    };
    struct SslDeleter {
        void operator()(SSL* ssl) const noexcept { SSL_shutdown(ssl); SSL_free(ssl); }
    };

    TcpSocket _tcp_socket {};
    std::unique_ptr<SSL_CTX, SslCtxDeleter> _ctx;
    std::unique_ptr<SSL, SslDeleter> _ssl;

public:
    TlsSocket();

    [[nodiscard]] std::expected<void, std::string> connect(const std::string& host, const std::string& port);

    [[nodiscard]] std::expected<ssize_t, std::string> send_data(const std::string& request) const;
    [[nodiscard]] std::expected<std::string, std::string> receive_data() const;
};
