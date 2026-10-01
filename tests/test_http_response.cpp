#include "http_response.hpp"
#include "test_framework.hpp"

namespace {
    std::span<const std::byte> bytes(const std::string_view s) { return std::as_bytes(std::span(s)); }
    std::string body(const HttpResponse &r) { return std::string(reinterpret_cast<const char *>(r.body.data()), r.body.size()); }
} // namespace

TEST(http_content_length_response) {
    const std::string raw = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\ncontent-length: 7\r\n\r\n{\"a\":1}";
    auto r = parse_http_response(bytes(raw));
    CHECK(r && *r);
    if (!r || !*r) return;
    CHECK_EQ((*r)->response.status, 200);
    CHECK((*r)->response.keep_alive);
    CHECK_EQ(body((*r)->response), std::string{"{\"a\":1}"});
    CHECK_EQ((*r)->bytes_consumed, raw.size());
}

TEST(http_every_prefix_needs_more_bytes) {
    const std::string raw = "HTTP/1.1 201 Created\r\nTransfer-Encoding: chunked\r\n\r\n4;ext=1\r\nabcd\r\n3\r\nefg\r\n0\r\nX-Trailer: y\r\n\r\n";
    for (size_t len = 0; len < raw.size(); ++len) {
        auto r = parse_http_response(bytes(std::string_view{raw}.substr(0, len)));
        CHECK(r.has_value() && !*r);
    }
    auto full = parse_http_response(bytes(raw));
    CHECK(full && *full && body((*full)->response) == "abcdefg" && (*full)->bytes_consumed == raw.size());
}

TEST(http_leaves_pipelined_bytes_unconsumed) {
    const std::string first = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi";
    auto r = parse_http_response(bytes(first + "HTTP/1.1 200 OK\r\n"));
    CHECK(r && *r && (*r)->bytes_consumed == first.size());
}

TEST(http_connection_semantics) {
    auto close = parse_http_response(bytes("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"));
    CHECK(close && *close && !(*close)->response.keep_alive);
    auto http10 = parse_http_response(bytes("HTTP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n"));
    CHECK(http10 && *http10 && !(*http10)->response.keep_alive);
    auto http10_ka = parse_http_response(bytes("HTTP/1.0 200 OK\r\nConnection: Keep-Alive\r\nContent-Length: 0\r\n\r\n"));
    CHECK(http10_ka && *http10_ka && (*http10_ka)->response.keep_alive);
}

TEST(http_close_delimited_body_needs_eof) {
    const std::string raw = "HTTP/1.1 200 OK\r\n\r\nuntil close";
    auto partial = parse_http_response(bytes(raw));
    CHECK(partial && !*partial);
    auto done = parse_http_response(bytes(raw), /*eof=*/true);
    CHECK(done && *done && body((*done)->response) == "until close" && !(*done)->response.keep_alive);
}

TEST(http_bodyless_statuses) {
    auto r = parse_http_response(bytes("HTTP/1.1 204 No Content\r\n\r\n"));
    CHECK(r && *r && (*r)->response.body.empty());
}

TEST(http_rejects_malformed_responses) {
    for (const char *bad: {"HTTP/2 200 OK\r\n\r\n", "HTTP/1.1 2x0 OK\r\n\r\n", "HTTP/1.1 200 OK\r\nNoColon\r\n\r\n",
                           "HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\n",
                           "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nxx",
                           "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n",
                           "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nabXX"})
        CHECK(!parse_http_response(bytes(bad)).has_value());
    CHECK(!parse_http_response(bytes("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort"), true).has_value());
    CHECK(!parse_http_response(bytes(std::string(kMaxHttpHeaderBytes + 1, 'x'))).has_value());
}
