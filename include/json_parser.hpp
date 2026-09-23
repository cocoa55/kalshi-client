#pragma once
#include <expected>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>


struct Token;
struct JsonValue;

using JsonObject = std::unordered_map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;


struct JsonValue {
    std::variant<std::monostate, bool, std::string, JsonArray, JsonObject> data;
};

std::expected<JsonValue, std::string> json_parser(std::span<const Token> tokens);