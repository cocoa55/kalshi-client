#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "http_response.hpp"
#include "json_parser.hpp"
#include "kalshi_auth.hpp"
#include "time_util.hpp"
#include "tls_socket.hpp"

// HTTPS client that keeps one TLS connection open across requests (HTTP/1.1 keep-alive).
// A fresh TCP + TLS handshake to Kalshi costs ~170 ms; reusing the connection takes that off every
// order. Requests are synchronous and one at a time, which matches the single-threaded bot.
//
// Retry policy: if sending fails on a *reused* connection (the server closed it while idle), the request
// is resent once on a new connection; nothing reached the server, so this is safe even for orders. A failure
// while *reading* the response is never retried: the order may have executed, and resending could double it.
class HttpClient {
    std::string _host;
    const kalshi_auth::Signer *_signer; // nullptr for unauthenticated requests
    std::optional<TlsSocket> _tls;
    std::vector<std::byte> _buffer;
    Clock::time_point _last_used{};
    Clock::time_point _last_send{};
    uint64_t _connections_opened{0};

    std::expected<void, std::string> open_connection();
    bool idle_connection_is_dead() const;
    std::expected<HttpResponse, std::string> read_response();

public:
    explicit HttpClient(std::string host, const kalshi_auth::Signer *signer = nullptr)
        : _host(std::move(host)), _signer(signer) {}

    // Opens the connection ahead of time so the first real request doesn't pay for the handshake.
    std::expected<void, std::string> connect();

    std::expected<HttpResponse, std::string> request(std::string_view method, std::string_view path,
                                                     std::string_view body = {});
    // request() + JSON parse of the body. Non-2xx responses are still returned as JSON (Kalshi error bodies).
    std::expected<JsonValue, std::string> request_json(std::string_view method, std::string_view path,
                                                       std::string_view body = {});

    bool connected() const { return _tls.has_value(); }
    uint64_t connections_opened() const { return _connections_opened; }
    Clock::duration idle_for() const { return Clock::now() - _last_used; }
    // When the most recent request's bytes were handed to the kernel (for latency measurement).
    Clock::time_point last_send_time() const { return _last_send; }
};
