#include "frame_builder.hpp"
#include "frame_parser.hpp"
#include "test_framework.hpp"

namespace {
    std::vector<std::byte> payload_of(const size_t n) {
        std::vector<std::byte> p(n);
        for (size_t i = 0; i < n; ++i) p[i] = static_cast<std::byte>(i * 31 + 7);
        return p;
    }
    std::vector<std::byte> raw(std::initializer_list<int> bytes) {
        std::vector<std::byte> out;
        for (int b: bytes) out.push_back(static_cast<std::byte>(b));
        return out;
    }
} // namespace

TEST(frame_round_trips_through_builder_and_parser) {
    // Covers all three length encodings and their boundaries (7-bit, 16-bit, 64-bit).
    for (const size_t n: {0uz, 1uz, 125uz, 126uz, 65535uz, 65536uz, 200'000uz}) {
        const WebSocketFrame frame{.fin_bit = true, .op_code = WebSocketFrame::Opcode::Binary, .payload = payload_of(n)};
        auto encoded = frame_builder(frame);
        CHECK(encoded.has_value());
        auto parsed = frame_parser(*encoded);
        CHECK(parsed.has_value());
        if (!parsed) continue;
        CHECK_EQ(parsed->bytes_consumed, encoded->size());
        CHECK(parsed->frame.payload == frame.payload); // unmasked correctly
        CHECK(parsed->frame.op_code == WebSocketFrame::Opcode::Binary);
    }
}

TEST(frame_every_prefix_is_incomplete) {
    const WebSocketFrame frame{.fin_bit = true, .op_code = WebSocketFrame::Opcode::Text, .payload = payload_of(300)};
    const auto encoded = *frame_builder(frame);
    for (size_t len = 0; len < encoded.size(); ++len) {
        auto r = frame_parser(std::span(encoded).first(len));
        CHECK(!r && r.error().kind == ParseError::Kind::Incomplete);
    }
}

TEST(frame_parser_consumes_only_one_frame) {
    auto a = *frame_builder({.fin_bit = true, .op_code = WebSocketFrame::Opcode::Text, .payload = payload_of(5)});
    const auto b = *frame_builder({.fin_bit = true, .op_code = WebSocketFrame::Opcode::Ping, .payload = payload_of(3)});
    const size_t first_size = a.size();
    a.insert(a.end(), b.begin(), b.end());
    auto r = frame_parser(a);
    CHECK(r && r->bytes_consumed == first_size);
}

TEST(frame_rejects_protocol_violations) {
    auto malformed = [](const std::vector<std::byte> &bytes) {
        auto r = frame_parser(bytes);
        return !r && r.error().kind == ParseError::Kind::Malformed;
    };
    CHECK(malformed(raw({0x83, 0x00})));                       // reserved opcode 0x3
    CHECK(malformed(raw({0xC1, 0x00})));                       // RSV1 set
    CHECK(malformed(raw({0x09, 0x00})));                       // fragmented ping (FIN clear)
    CHECK(malformed(raw({0x89, 0x7E, 0x00, 0x7E})));            // ping with 126-byte payload
    CHECK(malformed(raw({0x82, 0x7F, 0x80, 0, 0, 0, 0, 0, 0, 0}))); // 64-bit length with MSB set
    // A header claiming a huge (but legal-looking) payload is rejected up front instead of buffered forever.
    CHECK(malformed(raw({0x82, 0x7F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00})));
}
