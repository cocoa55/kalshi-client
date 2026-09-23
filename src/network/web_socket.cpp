#include "web_socket.hpp"
#include <array>
#include <chrono>
#include <format>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <print>
#include <string_view>
#include "kalshi_auth.hpp"

#include "frame_builder.hpp"
#include "frame_parser.hpp"

namespace {

constexpr std::string_view kWebSocketPath = "/trade-api/ws/v2";

std::expected<std::string, std::string>generate_websocket_key() {
    std::array<unsigned char, 16> raw{};
    if (RAND_bytes(raw.data(), raw.size()) != 1) {
        return std::unexpected("Failed to generate random bytes for WebSocket key");
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

    auto websocket_key_result = generate_websocket_key();
    if (!websocket_key_result.has_value()) {
        return std::unexpected(websocket_key_result.error());
    }
    const std::string websocket_key = websocket_key_result.value();

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
        "\r\n", kWebSocketPath, host, websocket_key_result.value(), credentials->key_id, timestamp, *signature);

    std::span<const std::byte> request_bytes{reinterpret_cast<const std::byte *>(upgrade_request.data()),
                                             upgrade_request.size()};

    auto send_result = _tls_socket.send_data(request_bytes);
    if (!send_result.has_value()) {
        return std::unexpected(std::format("Failed to send data {}", send_result.error()));
    }

    auto receive_result = _tls_socket.receive_data();
    if (!receive_result.has_value()) {
        return std::unexpected(std::format("Failed to receive data {}", receive_result.error()));
    }

    const std::string response(
            reinterpret_cast<const char*>(receive_result->data()),
            receive_result->size()
    );

    std::println("\n--- Kalshi Server Response ---");
    std::println("{}", response);
    std::println("------------------------------");

    if (!response.starts_with("HTTP/1.1 101")) {
        return std::unexpected("Handshake rejected (expected HTTP 101 Switching Protocols)");
    }

    return {};
}

std::expected<WebSocketFrame, std::string> WebSocket::receive_frame() {
    while (true) {

        auto result = frame_parser(_buffer); //parse whats in _buffer
        if (result.has_value()) {
            _buffer.erase(_buffer.begin(), _buffer.begin() + static_cast<std::ptrdiff_t>(result->bytes_consumed));
            return result->frame;
        }
        if (result.error().kind == ParseError::Kind::Malformed) {
            return std::unexpected(result.error().message);
        }
        //incomplete , read more bytes and append to _buffer
        auto raw_bytes = _tls_socket.receive_data();
        if (!raw_bytes.has_value()) {
            return std::unexpected(raw_bytes.error());
        }
        _buffer.insert(_buffer.end(),raw_bytes->begin(), raw_bytes->end());
    }
}

std::expected<void, std::string> WebSocket::send_frame(const WebSocketFrame& frame) {

auto out_going_frame = frame_builder(frame);
    if (!out_going_frame.has_value()) {
        return std::unexpected(out_going_frame.error());
    }

   auto send_result =  _tls_socket.send_data(out_going_frame.value());
    if (!send_result.has_value()) {
        return std::unexpected(send_result.error());
    }

    return {};
}

std::expected<void, std::string> WebSocket::send_pong(const WebSocketFrame& ping_frame) {
    WebSocketFrame pong_frame{.fin_bit = true,
                                     .op_code = WebSocketFrame::Opcode::Pong,
                                     .mask_key = std::nullopt,
                                     .payload = ping_frame.payload};
    return send_frame(pong_frame);
}
//std::expected<void, std::string> send_ping()

