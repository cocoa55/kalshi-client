// Properties: frame_parser never crashes or over-reads; a successful parse consumes at most the input and
// stays within the payload limit; and rebuilding the parsed frame then parsing it again yields the same payload.
#include <cstdint>
#include <cstdlib>

#include "frame_builder.hpp"
#include "frame_parser.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const auto input = std::as_bytes(std::span(data, size));
    auto parsed = frame_parser(input);
    if (!parsed)
        return 0;
    if (parsed->bytes_consumed > size || parsed->frame.payload.size() > kMaxFramePayload)
        std::abort();

    WebSocketFrame frame = parsed->frame;
    frame.fin_bit = true; // the builder always sets FIN
    auto rebuilt = frame_builder(frame);
    if (!rebuilt)
        std::abort();
    auto reparsed = frame_parser(*rebuilt);
    if (!reparsed || reparsed->frame.payload != frame.payload || reparsed->frame.op_code != frame.op_code ||
        reparsed->bytes_consumed != rebuilt->size())
        std::abort();
    return 0;
}
