#include "frame_parser.hpp"
#include "web_socket_frame.hpp"
#include <netinet/in.h>


std::expected<WebSocketFrame, std::string> frame_parser(std::span<const std::byte> bytes) {
        if (bytes.empty())
            return std::unexpected("Frame too short: no bytes to parse");


        if (bytes.size() < 2)
             return std::unexpected("Frame too short: missing byte 1");


        auto byte0 = bytes[0];
        bool fin = (byte0 & std::byte{0x80}) != std::byte{0};
        auto raw_opcode = static_cast<uint8_t>(byte0 & std::byte {0x0F});
        auto opcode = static_cast<WebSocketFrame::Opcode>(raw_opcode);

        auto byte1 = bytes[1];
        bool mask_bit = (byte1 & std::byte{0x80}) != std::byte{0};
        auto payload_len = static_cast<uint8_t>(byte1 & std::byte{0x7F});

        size_t pos = 2;
        uint64_t actual_payload_len{};

        switch (payload_len) {
            case 126: {
                if (pos + 2 > bytes.size())
                    return std::unexpected("Frame too short: missing extended length");
                const auto extended = bytes.subspan(pos, 2 );
                std::array<std::byte, 2> buf{};
                std::ranges::copy(extended, buf.begin());
                auto len = std::bit_cast<uint16_t>(buf);
                len = ntohs(len);
                pos += 2;
                actual_payload_len = len;
                break;
            }//read 2 bytes
            case 127: {
                if (pos + 8 > bytes.size())
                    return std::unexpected("Frame too short: missing extended 64-bit length");
                const auto extended = bytes.subspan(pos, 8);
                std::array<std::byte, 8> buf{};
                std::ranges::copy(extended, buf.begin());
                const auto len = std::bit_cast<uint64_t>(buf);
                actual_payload_len = be64toh(len);
                pos += 8;
                break;
            }//read 8 bytes
            default:
                actual_payload_len = payload_len;
                break;
        }

        std::optional<std::array<std::byte, 4>> mask_key;
        if (mask_bit) {
            if (pos + 4 > bytes.size()) {
                return std::unexpected("Frame too short: missing masking key");
            }
            std::array<std::byte , 4> raw_key{};
            std::ranges::copy(bytes.subspan(pos, 4), raw_key.begin());
            mask_key = raw_key;
            pos += 4;
        }

    if (pos + actual_payload_len > bytes.size()) {
        return std::unexpected("Frame too short: payload truncated");
    }

    auto payload_view = bytes.subspan(pos, actual_payload_len);
    std::vector<std::byte> payload(payload_view.begin(), payload_view.end());

        if (mask_bit) {
            for (auto i{0uz}; i < payload.size(); i++) {
                payload[i] ^= mask_key.value()[i % 4];
            }
        }

    return WebSocketFrame {
            .fin_bit = fin,
            .op_code = opcode,
            .mask_key = mask_key,
            .payload = std::move(payload)
        };
}
