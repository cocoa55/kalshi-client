#pragma once
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct HttpResponse {
    int status{};
    bool keep_alive{true};
    std::vector<std::byte> body;
};

struct ParsedHttpResponse {
    HttpResponse response;
    size_t bytes_consumed{};
};

inline constexpr size_t kMaxHttpHeaderBytes = 64 * 1024;
inline constexpr size_t kMaxHttpBodyBytes = 16 * 1024 * 1024;

// Incremental-friendly HTTP/1.1 response parser: call it with everything received so far.
// Returns nullopt when more bytes are needed, a response once one is complete, or an error for
// malformed input. The body is framed by Transfer-Encoding: chunked or Content-Length; without
// either, the body runs to connection close, so pass `eof = true` once the peer has closed.
// Pure function (no I/O), so it can be unit-tested and fuzzed directly.
std::expected<std::optional<ParsedHttpResponse>, std::string>
parse_http_response(std::span<const std::byte> data, bool eof = false);
