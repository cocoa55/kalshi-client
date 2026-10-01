#pragma once
#include <cstddef>
#include <expected>
#include <flat_map>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct JsonValue;

using JsonObject = std::flat_map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

// Numbers are kept as their original text (std::string) so callers can parse them exactly,
// e.g. with parse_fixed, instead of losing precision through double.
struct JsonValue {
    std::variant<std::monostate, bool, std::string, JsonArray, JsonObject> data;
};

// Nesting limit: the parser is recursive, so unbounded depth from untrusted input would overflow the stack.
inline constexpr int kMaxJsonDepth = 64;

// Parses exactly one JSON value; trailing non-whitespace is an error.
std::expected<JsonValue, std::string> parse_json(std::span<const std::byte> bytes);
std::expected<JsonValue, std::string> parse_json(std::string_view text);

// Decodes the escape sequences in a raw string token (including \uXXXX and surrogate pairs) to UTF-8.
// Expects text the lexer has already validated (escape letters and hex digits).
std::expected<std::string, std::string> decode_json_string(std::string_view raw);
