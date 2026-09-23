#pragma once
#include <expected>
#include <span>
#include <string>
#include <variant>
#include <vector>
#include <flat_map>

struct Token;
struct JsonValue;

using JsonObject = std::flat_map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;


struct JsonValue {
    std::variant<std::monostate, bool, std::string, JsonArray, JsonObject> data;
};

std::expected<JsonValue, std::string> json_parser(std::span<const Token> tokens);