#include "http_client.hpp"

#include <format>
#include <poll.h>

namespace {
    // Servers drop idle keep-alive connections after some timeout; reconnect pre-emptively before that.
    constexpr auto kMaxIdle = std::chrono::seconds{50};
} // namespace

std::expected<void, std::string> HttpClient::open_connection() {
    _tls.reset();
    _buffer.clear();
    TlsSocket tls;
    if (auto result = tls.connect(_host, "443"); !result)
        return std::unexpected(result.error());
    _tls.emplace(std::move(tls));
    ++_connections_opened;
    _last_used = Clock::now();
    return {};
}

std::expected<void, std::string> HttpClient::connect() {
    if (_tls && !idle_connection_is_dead())
        return {};
    return open_connection();
}

// HTTP/1.1 servers never send unsolicited bytes, so if an idle connection is readable the server has
// closed it (FIN or TLS close_notify). A zero-timeout poll() detects that without consuming anything.
bool HttpClient::idle_connection_is_dead() const {
    if (Clock::now() - _last_used > kMaxIdle)
        return true;
    pollfd pfd{.fd = _tls->fd(), .events = POLLIN, .revents = 0};
    return poll(&pfd, 1, 0) != 0;
}

std::expected<HttpResponse, std::string> HttpClient::read_response() {
    while (true) {
        auto parsed = parse_http_response(_buffer);
        if (!parsed)
            return std::unexpected(parsed.error());
        if (*parsed) {
            _buffer.erase(_buffer.begin(), _buffer.begin() + static_cast<std::ptrdiff_t>((*parsed)->bytes_consumed));
            return std::move((*parsed)->response);
        }

        auto chunk = _tls->receive_data();
        if (!chunk) {
            // Peer closed: a close-delimited body is now complete; anything else is an error.
            auto final = parse_http_response(_buffer, /*eof=*/true);
            if (final && *final)
                return std::move((*final)->response);
            return std::unexpected(std::format("reading response: {}", chunk.error()));
        }
        _buffer.insert(_buffer.end(), chunk->begin(), chunk->end());
    }
}

std::expected<HttpResponse, std::string> HttpClient::request(const std::string_view method, const std::string_view path,
                                                             const std::string_view body) {
    std::string auth;
    if (_signer) {
        auto headers = _signer->auth_headers(method, path);
        if (!headers)
            return std::unexpected(headers.error());
        auth = std::move(*headers);
    }
    std::string content_headers;
    if (!body.empty() || method == "POST")
        content_headers = std::format("Content-Type: application/json\r\nContent-Length: {}\r\n", body.size());

    const std::string request = std::format("{} {} HTTP/1.1\r\n"
                                            "Host: {}\r\n"
                                            "{}{}"
                                            "\r\n"
                                            "{}",
                                            method, path, _host, auth, content_headers, body);
    const std::span request_bytes{reinterpret_cast<const std::byte *>(request.data()), request.size()};

    bool reused = _tls && !idle_connection_is_dead();
    if (!reused) {
        if (auto opened = open_connection(); !opened)
            return std::unexpected(opened.error());
    }

    auto sent = _tls->send_data(request_bytes);
    if (!sent && reused) {
        if (auto opened = open_connection(); !opened)
            return std::unexpected(opened.error());
        sent = _tls->send_data(request_bytes);
    }
    if (!sent) {
        _tls.reset();
        return std::unexpected(std::format("sending request: {}", sent.error()));
    }
    _last_send = Clock::now();

    auto response = read_response();
    _last_used = Clock::now();
    if (!response || !response->keep_alive)
        _tls.reset();
    return response;
}

std::expected<JsonValue, std::string> HttpClient::request_json(const std::string_view method,
                                                               const std::string_view path,
                                                               const std::string_view body) {
    return request(method, path, body).and_then([](const HttpResponse &response) {
        return parse_json(response.body).transform_error([&](const std::string &error) {
            return std::format("HTTP {} with unparseable body: {}", response.status, error);
        });
    });
}
