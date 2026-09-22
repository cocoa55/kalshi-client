#include "tls_socket.hpp"
#include "net_constants.hpp"
#include <array>
#include <format>
#include "openssl_util.hpp"

TlsSocket::TlsSocket() : _ctx(SSL_CTX_new(TLS_client_method())) {
    if (_ctx) {
        SSL_CTX_set_default_verify_paths(_ctx.get());
        SSL_CTX_set_verify(_ctx.get(), SSL_VERIFY_PEER, nullptr);
    }
}

std::expected<void, std::string> TlsSocket::connect(const std::string& host, const std::string& port) {
    if (!_ctx) {
        return std::unexpected("Failed to create SSL context");
    }

    auto tcp_result = _tcp_socket.connect(host, port);
    if (!tcp_result.has_value()) {
        return std::unexpected(tcp_result.error());
    }

    _ssl.reset(SSL_new(_ctx.get()));
    if (!_ssl) {
        return std::unexpected("Failed to create SSL object");
    }

    if (SSL_set_fd(_ssl.get(), tcp_result.value()) != 1) {
        return std::unexpected("Failed to bind socket fd to SSL object");
    }

    // SNI: tells the server which hostname we're connecting to, required for
    // it to present the correct certificate.
    SSL_set_tlsext_host_name(_ssl.get(), host.c_str());
    SSL_set1_host(_ssl.get(), host.c_str());

    if (SSL_connect(_ssl.get()) != 1) {
        return std::unexpected(std::format("TLS handshake failed: {}", ssl_error_string()));
    }

    return {};
}

std::expected<ssize_t, std::string> TlsSocket::send_data(std::span<const std::byte> data) const {
    const int bytes = SSL_write(_ssl.get(), data.data(), static_cast<int>(data.size()));
    if (bytes <= 0) {
        return std::unexpected(std::format("SSL_write failed: {}", ssl_error_string()));
    }

    return bytes;
}

std::expected<std::vector<std::byte>, std::string> TlsSocket::receive_data() const {

    std::array<char, kReceiveBufferSize> buffer{};
    const int bytes = SSL_read(_ssl.get(), buffer.data(), static_cast<int>(buffer.size()));
    if (bytes <= 0) {
        return std::unexpected(std::format("SSL_read failed: {}", ssl_error_string()));
    }
    auto* begin = reinterpret_cast<const std::byte*>(buffer.data());
    return std::vector<std::byte>(begin, begin + bytes);
}
