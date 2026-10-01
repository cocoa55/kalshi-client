#include "http_response.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace {
    constexpr std::string_view kCrlf = "\r\n";

    // Header names and tokens are case-insensitive (RFC 9110).
    constexpr auto ichar_equal = [](const char x, const char y) {
        auto lower = [](const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };
        return lower(x) == lower(y);
    };

    bool iequals(const std::string_view a, const std::string_view b) { return std::ranges::equal(a, b, ichar_equal); }

    bool icontains(const std::string_view haystack, const std::string_view needle) {
        return std::ranges::contains_subrange(haystack, needle, ichar_equal);
    }

    std::string_view trim(std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
        return s;
    }

    struct Headers {
        int status{};
        bool http10{false};
        std::optional<size_t> content_length;
        bool chunked{false};
        std::optional<bool> connection_keep_alive; // from the Connection header, if present
    };

    std::expected<Headers, std::string> parse_headers(std::string_view head) {
        Headers h;
        const size_t line_end = head.find(kCrlf);
        const std::string_view status_line = head.substr(0, line_end);
        // "HTTP/1.1 200 OK"
        if (status_line.size() < 12 || !status_line.starts_with("HTTP/1.") || status_line[8] != ' ')
            return std::unexpected("Malformed HTTP status line");
        h.http10 = status_line[7] == '0';
        const auto code = status_line.substr(9, 3);
        if (std::from_chars(code.data(), code.data() + 3, h.status).ec != std::errc{} || h.status < 100 ||
            h.status > 599)
            return std::unexpected("Malformed HTTP status code");

        size_t pos = line_end == std::string_view::npos ? head.size() : line_end + 2;
        while (pos < head.size()) {
            size_t end = head.find(kCrlf, pos);
            if (end == std::string_view::npos)
                end = head.size();
            const std::string_view line = head.substr(pos, end - pos);
            pos = end + 2;

            const size_t colon = line.find(':');
            if (colon == std::string_view::npos || colon == 0)
                return std::unexpected("Malformed HTTP header line");
            const std::string_view name = line.substr(0, colon);
            const std::string_view value = trim(line.substr(colon + 1));

            if (iequals(name, "content-length")) {
                size_t len{};
                const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), len);
                if (ec != std::errc{} || ptr != value.data() + value.size())
                    return std::unexpected("Malformed Content-Length");
                // Conflicting duplicate lengths are a request-smuggling vector; reject them.
                if (h.content_length && *h.content_length != len)
                    return std::unexpected("Conflicting Content-Length headers");
                h.content_length = len;
            } else if (iequals(name, "transfer-encoding")) {
                h.chunked = icontains(value, "chunked");
            } else if (iequals(name, "connection")) {
                if (icontains(value, "close"))
                    h.connection_keep_alive = false;
                else if (icontains(value, "keep-alive"))
                    h.connection_keep_alive = true;
            }
        }
        return h;
    }

    // Decodes a chunked body starting at `pos`. Returns the index just past the final CRLF, or nullopt if incomplete.
    std::expected<std::optional<size_t>, std::string> decode_chunked(std::string_view data, size_t pos,
                                                                     std::vector<std::byte> &body) {
        while (true) {
            const size_t line_end = data.find(kCrlf, pos);
            if (line_end == std::string_view::npos) {
                if (data.size() - pos > 1024)
                    return std::unexpected("Chunk size line too long");
                return std::optional<size_t>{};
            }
            std::string_view size_str = data.substr(pos, line_end - pos);
            size_str = size_str.substr(0, size_str.find(';')); // drop chunk extensions
            size_str = trim(size_str);
            size_t chunk_size{};
            const auto [ptr, ec] = std::from_chars(size_str.data(), size_str.data() + size_str.size(), chunk_size, 16);
            if (size_str.empty() || ec != std::errc{} || ptr != size_str.data() + size_str.size())
                return std::unexpected("Malformed chunk size");
            pos = line_end + 2;

            if (chunk_size == 0) {
                // Optional trailer headers, terminated by an empty line.
                while (true) {
                    const size_t trailer_end = data.find(kCrlf, pos);
                    if (trailer_end == std::string_view::npos)
                        return std::optional<size_t>{};
                    const bool empty_line = trailer_end == pos;
                    pos = trailer_end + 2;
                    if (empty_line)
                        return std::optional<size_t>{pos};
                }
            }
            if (chunk_size > kMaxHttpBodyBytes || body.size() + chunk_size > kMaxHttpBodyBytes)
                return std::unexpected("HTTP body too large");
            if (data.size() - pos < chunk_size + 2)
                return std::optional<size_t>{};
            if (data.substr(pos + chunk_size, 2) != kCrlf)
                return std::unexpected("Missing CRLF after chunk data");
            const auto *chunk = reinterpret_cast<const std::byte *>(data.data() + pos);
            body.insert(body.end(), chunk, chunk + chunk_size);
            pos += chunk_size + 2;
        }
    }
} // namespace

std::expected<std::optional<ParsedHttpResponse>, std::string> parse_http_response(std::span<const std::byte> bytes,
                                                                                  const bool eof) {
    const std::string_view data{reinterpret_cast<const char *>(bytes.data()), bytes.size()};

    const size_t header_end = data.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        if (data.size() > kMaxHttpHeaderBytes)
            return std::unexpected("HTTP headers too large");
        if (eof)
            return std::unexpected("Connection closed before HTTP headers were complete");
        return std::nullopt;
    }
    if (header_end > kMaxHttpHeaderBytes)
        return std::unexpected("HTTP headers too large");

    auto headers = parse_headers(data.substr(0, header_end));
    if (!headers)
        return std::unexpected(headers.error());

    const size_t body_start = header_end + 4;
    ParsedHttpResponse parsed;
    parsed.response.status = headers->status;
    parsed.response.keep_alive = headers->connection_keep_alive.value_or(!headers->http10);

    if (headers->chunked) {
        auto end = decode_chunked(data, body_start, parsed.response.body);
        if (!end)
            return std::unexpected(end.error());
        if (!*end) {
            if (eof)
                return std::unexpected("Connection closed mid-chunked body");
            return std::nullopt;
        }
        parsed.bytes_consumed = **end;
        return parsed;
    }

    if (headers->content_length) {
        const size_t len = *headers->content_length;
        if (len > kMaxHttpBodyBytes)
            return std::unexpected("HTTP body too large");
        if (data.size() - body_start < len) {
            if (eof)
                return std::unexpected("Connection closed before HTTP body was complete");
            return std::nullopt;
        }
        const auto *body = bytes.data() + body_start;
        parsed.response.body.assign(body, body + len);
        parsed.bytes_consumed = body_start + len;
        return parsed;
    }

    // 1xx, 204 and 304 never have a body.
    if (headers->status < 200 || headers->status == 204 || headers->status == 304) {
        parsed.bytes_consumed = body_start;
        return parsed;
    }

    // No framing: body is delimited by connection close, so the connection can't be reused.
    parsed.response.keep_alive = false;
    if (!eof)
        return std::nullopt;
    if (data.size() - body_start > kMaxHttpBodyBytes)
        return std::unexpected("HTTP body too large");
    parsed.response.body.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body_start), bytes.end());
    parsed.bytes_consumed = bytes.size();
    return parsed;
}
