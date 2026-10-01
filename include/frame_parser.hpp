#pragma once
#include <expected>
#include <cstdint>
#include <span>
#include <string>

#include "web_socket_frame.hpp"
#include "parse_error.hpp"

// Frames larger than this are rejected as malformed instead of buffered: a header can claim a 2^63-byte
// payload, and waiting for it would grow the receive buffer without bound.
inline constexpr uint64_t kMaxFramePayload = 16 * 1024 * 1024;

struct ParseResult {
    WebSocketFrame frame;
    size_t bytes_consumed{};
};

std::expected<ParseResult,ParseError> frame_parser(std::span<const std::byte> bytes);
