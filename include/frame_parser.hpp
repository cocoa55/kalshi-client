#pragma once
#include <expected>
#include <span>
#include <string>

#include "web_socket_frame.hpp"
#include "parse_error.hpp"

struct ParseResult {
    WebSocketFrame frame;
    size_t bytes_consumed{};
};

std::expected<ParseResult,ParseError> frame_parser(std::span<const std::byte> bytes);
