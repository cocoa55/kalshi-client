
#include "../../include/frame_builder.hpp"

#include <openssl/rand.h>

std::expected<std::vector<std::byte>, std::string> frame_builder(const WebSocketFrame& frame) {
    std::vector<std::byte> outgoing_frame;

    size_t header_size = 2; // byte0 + byte1
    if (frame.payload.size() > 65535) header_size += 8;
    else if (frame.payload.size() > 125) header_size += 2;
    outgoing_frame.reserve(header_size + 4 + frame.payload.size());

    auto byte0 = std::byte {0x80} | static_cast<std::byte>(frame.op_code);
    outgoing_frame.push_back(byte0); // FIN=1, opcode=Pong



    if (frame.payload.size() <= 125) {
        outgoing_frame.push_back(std::byte{0x80} | std::byte{static_cast<uint8_t>(frame.payload.size())});

    } else if (frame.payload.size() <= 65535) {
        outgoing_frame.push_back(std::byte{0x80} | std::byte{126});
        auto len = static_cast<uint16_t>(frame.payload.size());

        if (std::endian::native != std::endian::big) {
            len = std::byteswap(len);
        }

        auto len_bytes = std::bit_cast<std::array<std::byte, 2>>(len);
        std::ranges::copy(len_bytes, std::back_inserter(outgoing_frame));

    } else {
        outgoing_frame.push_back(std::byte {0x80} | std::byte {127});
        auto len = static_cast<uint64_t>(frame.payload.size());

        if (std::endian::native != std::endian::big) {
            len = std::byteswap(len);
        }

        auto len_bytes = std::bit_cast<std::array<std::byte, 8>>(len);
        std::ranges::copy(len_bytes, std::back_inserter(outgoing_frame));
    }


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