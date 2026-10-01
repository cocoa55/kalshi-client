// Differential fuzzer: the direct (tree-free) decoder must accept exactly the inputs the JSON-tree path
// accepts, and produce identical messages for them.
#include <cstdint>
#include <cstdlib>

#include "json_parser.hpp"
#include "kalshi_messages.hpp"
#include "message_compare.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const auto input = std::as_bytes(std::span(data, size));
    auto direct = decode_kalshi_message(input);
    auto json = parse_json(input);
    auto via_tree = json ? parse_kalshi_message(*json) : std::unexpected(json.error());
    if (direct.has_value() != via_tree.has_value())
        std::abort();
    if (direct && !same_message(*direct, *via_tree))
        std::abort();
    return 0;
}
