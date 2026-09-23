#include "frame_parser.hpp"
#include <bit>
#include <format>
#include "web_socket_frame.hpp"
std::expected<ParseResult, ParseError> frame_parser(std::span<const std::byte> bytes) {
    constexpr std::array<uint8_t, 6> valid_opcodes{0x0, 0x1, 0x2, 0x8, 0x9, 0xA};

    if (bytes.empty())
        return std::unexpected(
                ParseError{.kind = ParseError::Kind::Incomplete, .message = "No bytes available to parse"});


    if (bytes.size() < 2)
        return std::unexpected(ParseError{.kind = ParseError::Kind::Incomplete,
                                          .message = "Frame header incomplete: only 1 byte received"});


    auto byte0 = bytes[0];
    bool fin = (byte0 & std::byte{0x80}) != std::byte{0};
    auto raw_opcode = static_cast<uint8_t>(byte0 & std::byte{0x0F});
    if (std::ranges::find(valid_opcodes, raw_opcode) == valid_opcodes.end()) {
        return std::unexpected(
                ParseError{.kind = ParseError::Kind::Malformed,
                           .message = std::format("Malformed frame: unrecognized opcode {:#04x}", raw_opcode)});
    }
    auto opcode = static_cast<WebSocketFrame::Opcode>(raw_opcode);

    auto byte1 = bytes[1];
    bool mask_bit = (byte1 & std::byte{0x80}) != std::byte{0};
    auto payload_len = static_cast<uint8_t>(byte1 & std::byte{0x7F});

    size_t pos = 2;
    uint64_t actual_payload_len{};

    switch (payload_len) {
        case 126: {
            if (pos + 2 > bytes.size())
                return std::unexpected(
                        ParseError{.kind = ParseError::Kind::Incomplete,
                                   .message = "Frame header incomplete: missing 2-byte extended length field"});
            const auto extended = bytes.subspan(pos, 2);
            std::array<std::byte, 2> buf{};
            std::ranges::copy(extended, buf.begin());
            auto len = std::bit_cast<uint16_t>(buf);

            if (std::endian::native != std::endian::big) {
                len = std::byteswap(len);
            }
            pos += 2;
            actual_payload_len = len;
            break;
        } // read 2 bytes
        case 127: {
            if (pos + 8 > bytes.size())
                return std::unexpected(
                        ParseError{.kind = ParseError::Kind::Incomplete,
                                   .message = "Frame header incomplete: missing 8-byte extended length field"});
            const auto extended = bytes.subspan(pos, 8);
            std::array<std::byte, 8> buf{};
            std::ranges::copy(extended, buf.begin());
            auto len = std::bit_cast<uint64_t>(buf);
            if (std::endian::native != std::endian::big) {
                len = std::byteswap(len);
            }
            actual_payload_len = len;
            pos += 8;
            break;
        } // read 8 bytes
        default:
            actual_payload_len = payload_len;
            break;
    }

    std::optional<std::array<std::byte, 4>> mask_key;
    if (mask_bit) {
        if (pos + 4 > bytes.size()) {
            return std::unexpected(ParseError{.kind = ParseError::Kind::Incomplete,
                                              .message = "Frame header incomplete: missing 4-byte masking key"});
        }
        std::array<std::byte, 4> raw_key{};
        std::ranges::copy(bytes.subspan(pos, 4), raw_key.begin());
        mask_key = raw_key;
        pos += 4;
    }

    if (actual_payload_len > bytes.size() - pos) {
        return std::unexpected(
                ParseError{.kind = ParseError::Kind::Incomplete,
                           .message = "Frame payload incomplete: received fewer bytes than indicated by length field"});
    }

    auto payload_view = bytes.subspan(pos, actual_payload_len);
    std::vector<std::byte> payload(payload_view.begin(), payload_view.end());

    if (mask_bit) {
        for (auto i{0uz}; i < payload.size(); i++) {
            payload[i] ^= mask_key.value()[i % 4];
        }
    }

    return ParseResult{.frame = WebSocketFrame{.fin_bit = fin,
                                               .op_code = opcode,
                                               .mask_key = mask_key,
                                               .payload = std::move(payload)},
                       .bytes_consumed = pos + actual_payload_len};
}
