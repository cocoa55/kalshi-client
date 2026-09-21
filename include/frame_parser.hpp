#pragma once
#include <expected>
#include <span>
#include <string>

#include "web_socket_frame.hpp"


std::expected<WebSocketFrame, std::string> frame_parser(std::span<const std::byte> bytes);
