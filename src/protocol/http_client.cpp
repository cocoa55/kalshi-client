#include "http_client.hpp"

#include <sys/socket.h>

#include "json_lexer.hpp"
#include "time_util.hpp"
#include "tls_socket.hpp"

std::expected<JsonValue, std::string> http_post(std::string_view host, std::string_view path, std::string_view body, const kalshi_auth::Credentials& credentials) {
        TlsSocket tls;
        auto tls_result = tls.connect(std::string(host), "443");
        if (!tls_result.has_value()) return std::unexpected(tls_result.error());

    const std::string timestamp = current_timestamp_ms();
    const std::string signing_message = timestamp + "POST" + std::string(path);

    auto signature = kalshi_auth::sign(credentials.private_key_path, signing_message);
    if (!signature.has_value()) return std::unexpected("Failed to sign request: " + signature.error());

    const std::string request = std::format("POST {} HTTP/1.1\r\n"
                                            "Host: {}\r\n"
                                            "Content-Type: application/json\r\n"
                                            "Content-Length: {}\r\n"
                                            "KALSHI-ACCESS-KEY: {}\r\n"
                                            "KALSHI-ACCESS-SIGNATURE: {}\r\n"
                                            "KALSHI-ACCESS-TIMESTAMP: {}\r\n"
                                            "Connection: close\r\n"
                                            "\r\n"
                                            "{}",
                                            path, host, body.size(), credentials.key_id, *signature, timestamp, body);

    std::span request_bytes {
      reinterpret_cast<const std::byte*>(request.data()),
        request.size()
    };

    auto send_result = tls.send_data(request_bytes);

    if (!send_result.has_value()) return std::unexpected("Failed to send upgrade request: " + send_result.error());

    std::vector<std::byte> response_bytes;

    while (true) { //Read the response
        auto chunk = tls.receive_data();
        if (!chunk.has_value()) break;
        response_bytes.insert(response_bytes.end(), chunk->begin(), chunk->end());
    }

    std::array<std::byte, 4> seperator {
      std::byte{'\r'} , std::byte{'\n'}, std::byte{'\r'}, std::byte{'\n'}
      };


    auto result = std::ranges::search(response_bytes, seperator);
    if (result.empty()) return std::unexpected("Invalid HTTP response: no header/body seperator");

    std::span<const std::byte> body_bytes(result.end(), response_bytes.end());

    auto tokens = json_lexer(body_bytes);
    if (!tokens.has_value()) return std::unexpected(tokens.error());

    auto json = json_parser(*tokens);
    if (!json.has_value()) return std::unexpected(json.error());

    return json;
}


std::expected<JsonValue, std::string> http_get(const std::string_view host, const std::string_view path,
                                               const kalshi_auth::Credentials &credentials) {
    TlsSocket tls;
    auto tls_result = tls.connect(std::string(host), "443");
    if (!tls_result.has_value()) return std::unexpected(tls_result.error());

    const std::string timestamp = current_timestamp_ms();
    const std::string signing_message = timestamp + "GET" + std::string(path);

   auto signature = kalshi_auth::sign(credentials.private_key_path, signing_message);

    if (!signature.has_value()) return std::unexpected("Failed to sign request: " + signature.error());

    const std::string request = std::format(
                                "GET {} HTTP/1.1\r\n"
                                       "Host: {}\r\n"
                                       "KALSHI-ACCESS-KEY: {}\r\n"
                                       "KALSHI-ACCESS-SIGNATURE: {}\r\n"
                                       "KALSHI-ACCESS-TIMESTAMP: {}\r\n"
                                       "Connection: close\r\n"
                                       "\r\n",
                                       path, host, credentials.key_id, *signature, timestamp);
    std::span request_bytes {
        reinterpret_cast<const std::byte*>(request.data()),
          request.size()
      };

    auto send_result = tls.send_data(request_bytes);

    if (!send_result.has_value()) return std::unexpected("Failed to send upgrade request: " + send_result.error());

    std::vector<std::byte> response_bytes;

    while (true) { //Read the response
        auto chunk = tls.receive_data();
        if (!chunk.has_value()) break;
        response_bytes.insert(response_bytes.end(), chunk->begin(), chunk->end());
    }

    std::array<std::byte, 4> seperator {
        std::byte{'\r'} , std::byte{'\n'}, std::byte{'\r'}, std::byte{'\n'}
    };


    auto result = std::ranges::search(response_bytes, seperator);
    if (result.empty()) return std::unexpected("Invalid HTTP response: no header/body seperator");

    std::span<const std::byte> body_bytes(result.end(), response_bytes.end());

    auto tokens = json_lexer(body_bytes);
    if (!tokens.has_value()) return std::unexpected(tokens.error());

    auto json = json_parser(*tokens);
    if (!json.has_value()) return std::unexpected(json.error());

    return json;
}
