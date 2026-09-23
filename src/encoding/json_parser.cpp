#include "../../include/json_parser.hpp"

#include "json_lexer.hpp"

namespace {
    // forward declarations
    std::expected<JsonObject, std::string> parse_object(std::span<const Token> tokens, size_t &pos);
    std::expected<JsonArray, std::string> parse_array(std::span<const Token> tokens, size_t &pos);
    std::expected<JsonValue, std::string> parse_value(std::span<const Token> tokens, size_t &pos);
    // implementations below


    std::expected<JsonValue, std::string> parse_value(std::span<const Token> tokens, size_t &pos) {
        if (pos >= tokens.size()) {
            return std::unexpected("Unexpected end of JSON tokens");
        }
        switch (tokens[pos].type) {
            case TokenType::LeftBrace: {
                auto obj = parse_object(tokens, pos);
                if (!obj.has_value()) return std::unexpected(obj.error());
                return JsonValue{.data = std::move(obj.value())};
            }
            case TokenType::LeftBracket: {
                auto arr =parse_array(tokens, pos);
                if (!arr.has_value()) return std::unexpected(arr.error());
                    return JsonValue{.data = std::move(arr.value())};
                }
                case TokenType::String:
                case TokenType::Number:
                    return JsonValue{.data = tokens[pos++].value};
                case TokenType::True:
                    pos++;
                    return JsonValue{.data = true};
                case TokenType::False:
                    pos++;
                    return JsonValue{.data = false};
                case TokenType::Null:
                    pos++;
                    return JsonValue{.data = std::monostate{}};
                default:
                    return std::unexpected("Unexpected token encountered in parse_value");
            }
    }

    std::expected<JsonObject, std::string> parse_object(std::span<const Token> tokens, size_t &pos) {
        JsonObject object {};
        ++pos;
        if (tokens[pos].type == TokenType::RightBrace) {
            ++pos;
            return JsonObject{};
        }
        while (pos < tokens.size() && tokens[pos].type != TokenType::RightBrace) {
            if (tokens[pos].type != TokenType::String) {
                return std::unexpected("Expected string key in object");
            }
            std::string key = tokens[pos].value;
            ++pos;
            if (pos >= tokens.size() || tokens[pos].type != TokenType::Colon) {
                return std::unexpected("Expected ':' after object key");
            }
            ++pos;
            auto value = parse_value(tokens, pos);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            object.emplace(std::move(key), std::move(value.value()));
            if (pos < tokens.size()) {
                if (tokens[pos].type == TokenType::Comma) {
                    ++pos;
                }
                else if (tokens[pos].type == TokenType::RightBrace) {
                    //Do nothing
                }else {
                    return std::unexpected("Expected ',' or '}' after object value");
                }
            }
        }
        ++pos;
        return object;
    }
    std::expected<JsonArray, std::string> parse_array(std::span<const Token> tokens, size_t &pos) {

        JsonArray array{};
        ++pos;
        if (tokens[pos].type == TokenType::RightBracket) {
            ++pos;
            return array;
        }
        while (pos < tokens.size() && tokens[pos].type != TokenType::RightBracket) {
            auto value = parse_value(tokens, pos);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            array.push_back(std::move(value.value()));

            if (pos < tokens.size()) {
                if (tokens[pos].type == TokenType::Comma) {
                    ++pos;
                }
                else if (tokens[pos].type == TokenType::RightBracket) {
                    //Do nothing
                }
                else {
                    return std::unexpected("Expected ',' or ']' after array value");
                }
            }
        }
        pos++;
        return array;
    }
}


    std::expected<JsonValue, std::string> json_parser(std::span<const Token> tokens) {
        if (tokens.empty()) return std::unexpected("Empty tokens");
        auto pos{0uz};
        return parse_value(tokens, pos);
    }
