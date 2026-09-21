
#include "../../include/frame_builder.hpp"

#include <openssl/rand.h>

std::expected<std::vector<std::byte>, std::string> frame_builder(const WebSocketFrame& frame) {
    std::vector<std::byte> outgoing_frame;
    outgoing_frame.reserve(2 + 4 + frame.payload.size());

    auto byte0 = std::byte {0x80} | static_cast<std::byte>(frame.op_code);
    outgoing_frame.push_back(byte0); // FIN=1, opcode=Pong

    auto byte1 = std::byte{0x80} | std::byte{static_cast<uint8_t>(frame.payload.size())};
    outgoing_frame.push_back(byte1);

    std::array<unsigned char, 4> mask_raw {};

    if (RAND_bytes(mask_raw.data(), 4) != 1)
        return std::unexpected("Failed to generate masking key");

    auto mask_key = std::bit_cast<std::array<std::byte, 4>>(mask_raw);

    std::ranges::copy(mask_key, std::back_inserter(outgoing_frame));

    for (auto i{0uz}; i < frame.payload.size(); i++) {
        outgoing_frame.push_back(frame.payload[i] ^ mask_key[i % 4]);
    }

return outgoing_frame;
}