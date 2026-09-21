#pragma once
#include <cstddef>
#include <web_socket.hpp>

std::expected<std::vector<std::byte>, std::string> frame_builder(const WebSocketFrame& frame);

