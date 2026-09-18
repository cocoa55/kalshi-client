#include "web_socket.hpp"
#include "kalshi_auth.hpp"
#include <print>
#include <format>
#include <array>
#include <chrono>
#include <string_view>
#include <stdexcept>
#include <openssl/rand.h>
#include <openssl/evp.h>

namespace {

constexpr std::string_view kWebSocketPath = "/trade-api/ws/v2";

std::string generate_websocket_key() {
    std::array<unsigned char, 16> raw{};
    if (RAND_bytes(raw.data(), raw.size()) != 1) {
        throw std::runtime_error("Failed to generate random bytes for WebSocket key");
    }

    std::array<unsigned char, 25> encoded{};
    const int len = EVP_EncodeBlock(encoded.data(), raw.data(), raw.size());

    return std::string{reinterpret_cast<char*>(encoded.data()), static_cast<size_t>(len)};
}

std::string current_timestamp_ms() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return std::to_string(ms.count());
}

}

std::expected<void, std::string> WebSocket::connect(const std::string &host, const std::string &port) {

    auto credentials = kalshi_auth::load_credentials_from_env();
    if (!credentials.has_value()) {
        return std::unexpected(std::format("Missing Kalshi credentials: {}", credentials.error()));
    }

    const std::string timestamp = current_timestamp_ms();
    const std::string signing_message = timestamp + "GET" + std::string{kWebSocketPath};

    auto signature = kalshi_auth::sign(credentials->private_key_path, signing_message);
    if (!signature.has_value()) {
        return std::unexpected(std::format("Failed to sign request: {}", signature.error()));
    }

    auto tls_result = _tls_socket.connect(host, port);
    if (!tls_result.has_value()) {
        return std::unexpected(std::format("TLS connection failed {}", tls_result.error()));
    }

    const std::string websocket_key = generate_websocket_key();

    const std::string upgrade_request = std::format(
        "GET {} HTTP/1.1\r\n"
        "Host: {}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: {}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "KALSHI-ACCESS-KEY: {}\r\n"
        "KALSHI-ACCESS-TIMESTAMP: {}\r\n"
        "KALSHI-ACCESS-SIGNATURE: {}\r\n"
        "\r\n", kWebSocketPath, host, websocket_key, credentials->key_id, timestamp, *signature);

    auto send_result = _tls_socket.send_data(upgrade_request);
    if (!send_result.has_value()) {
        return std::unexpected(std::format("Failed to send data {}", send_result.error()));
    }

    auto receive_result = _tls_socket.receive_data();
    if (!receive_result.has_value()) {
        return std::unexpected(std::format("Failed to receive data {}", receive_result.error()));
    }

    const std::string &response = receive_result.value();

    std::println("\n--- Kalshi Server Response ---");
    std::println("{}", response);
    std::println("------------------------------");

    if (!response.starts_with("HTTP/1.1 101")) {
        return std::unexpected("Handshake rejected (expected HTTP 101 Switching Protocols)");
    }

    return {};
}
