// Property: parse_json never crashes on arbitrary bytes, and anything it accepts survives a
// print -> parse -> print round trip unchanged.
#include <cstdint>
#include <cstdlib>

#include "json_parser.hpp"
#include "kalshi_messages.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const auto input = std::as_bytes(std::span(data, size));
    auto first = parse_json(input);
    if (!first)
        return 0;
    const std::string dumped = to_json_string(*first);
    auto second = parse_json(std::string_view{dumped});
    if (!second || to_json_string(*second) != dumped)
        std::abort();
    return 0;
}
