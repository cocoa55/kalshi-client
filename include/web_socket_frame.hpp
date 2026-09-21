#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

struct WebSocketFrame {
    enum class Opcode : uint8_t {
        Continuation = 0x0,
        Text = 0x1,
        Binary = 0x2,
        Close = 0x8,
        Ping = 0x9,
        Pong = 0xA
    };

    bool fin_bit;
    Opcode op_code;
    std::optional<std::array<std::byte, 4>> mask_key;
    std::vector<std::byte> payload;

};