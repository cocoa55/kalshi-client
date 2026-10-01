// Properties: parse_http_response never crashes; never claims to consume more than it was given; and is
// consistent under incremental delivery: if a prefix already yields a complete response, the full input
// must yield the same response with the same byte count (the client relies on this when reading in chunks).
#include <cstdint>
#include <cstdlib>

#include "http_response.hpp"

namespace {
    bool same(const ParsedHttpResponse &a, const ParsedHttpResponse &b) {
        return a.bytes_consumed == b.bytes_consumed && a.response.status == b.response.status &&
               a.response.keep_alive == b.response.keep_alive && a.response.body == b.response.body;
    }
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const auto input = std::as_bytes(std::span(data, size));
    for (const bool eof: {false, true}) {
        auto full = parse_http_response(input, eof);
        if (full && *full && (*full)->bytes_consumed > size)
            std::abort();
    }

    auto full = parse_http_response(input);
    for (size_t cut: {size / 4, size / 2, size - size / 4}) {
        auto prefix = parse_http_response(input.first(cut));
        if (prefix && *prefix) {
            if (!full || !*full || !same(**prefix, **full))
                std::abort();
        }
    }
    return 0;
}
