#include "json_parser.hpp"

#include <cstdint>

#include "json_lexer.hpp"

namespace {
    uint32_t hex4(std::string_view s) {
        uint32_t v = 0;
        for (const char c: s.substr(0, 4)) {
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else v |= c - 'A' + 10; // lexer already validated hex
        }
        return v;
    }

    void append_utf8(std::string &out, const uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    // Recursive descent over a pull lexer with one token of lookahead held by the caller.
    class Parser {
        JsonLexer _lexer;

        std::expected<JsonValue, JsonError> parse_value(const Token &token, int depth) {
            switch (token.type) {
                case TokenType::LeftBrace:
                case TokenType::LeftBracket: {
                    if (depth >= kMaxJsonDepth)
                        return fail("JSON nested too deeply");
                    if (token.type == TokenType::LeftBrace) {
                        auto obj = parse_object(depth + 1);
                        if (!obj) return std::unexpected(obj.error());
                        return JsonValue{.data = std::move(*obj)};
                    }
                    auto arr = parse_array(depth + 1);
                    if (!arr) return std::unexpected(arr.error());
                    return JsonValue{.data = std::move(*arr)};
                }
                case TokenType::String: {
                    auto s = string_value(token);
                    if (!s) return std::unexpected(s.error());
                    return JsonValue{.data = std::move(*s)};
                }
                case TokenType::Number: return JsonValue{.data = std::string{token.text}};
                case TokenType::True: return JsonValue{.data = true};
                case TokenType::False: return JsonValue{.data = false};
                case TokenType::Null: return JsonValue{.data = std::monostate{}};
                case TokenType::End: return fail("Unexpected end of JSON");
                default: return fail("Unexpected token in value position");
            }
        }

        std::unexpected<JsonError> fail(const char *what) const {
            return std::unexpected(JsonError{what, _lexer.position()});
        }

        std::expected<std::string, JsonError> string_value(const Token &token) const {
            if (!token.has_escapes)
                return std::string{token.text};
            auto decoded = decode_json_string(token.text);
            if (!decoded)
                return fail("Invalid escape sequence in string");
            return std::move(*decoded);
        }

        std::expected<JsonObject, JsonError> parse_object(const int depth) {
            JsonObject object;
            auto token = _lexer.next();
            if (!token) return std::unexpected(token.error());
            if (token->type == TokenType::RightBrace)
                return object;

            while (true) {
                if (token->type != TokenType::String)
                    return fail("Expected string key in object");
                auto key = string_value(*token);
                if (!key) return std::unexpected(key.error());

                auto colon = _lexer.next();
                if (!colon) return std::unexpected(colon.error());
                if (colon->type != TokenType::Colon)
                    return fail("Expected ':' after object key");

                auto value_token = _lexer.next();
                if (!value_token) return std::unexpected(value_token.error());
                auto value = parse_value(*value_token, depth);
                if (!value) return std::unexpected(value.error());
                object.emplace(std::move(*key), std::move(*value));

                auto sep = _lexer.next();
                if (!sep) return std::unexpected(sep.error());
                if (sep->type == TokenType::RightBrace)
                    return object;
                if (sep->type != TokenType::Comma)
                    return fail("Expected ',' or '}' after object value");
                token = _lexer.next();
                if (!token) return std::unexpected(token.error());
            }
        }

        std::expected<JsonArray, JsonError> parse_array(const int depth) {
            JsonArray array;
            auto token = _lexer.next();
            if (!token) return std::unexpected(token.error());
            if (token->type == TokenType::RightBracket)
                return array;

            while (true) {
                auto value = parse_value(*token, depth);
                if (!value) return std::unexpected(value.error());
                array.push_back(std::move(*value));

                auto sep = _lexer.next();
                if (!sep) return std::unexpected(sep.error());
                if (sep->type == TokenType::RightBracket)
                    return array;
                if (sep->type != TokenType::Comma)
                    return fail("Expected ',' or ']' after array value");
                token = _lexer.next();
                if (!token) return std::unexpected(token.error());
            }
        }

    public:
        explicit Parser(std::string_view text) : _lexer(text) {}

        std::expected<JsonValue, JsonError> parse() {
            auto first = _lexer.next();
            if (!first) return std::unexpected(first.error());
            auto value = parse_value(*first, 0);
            if (!value) return value;
            auto end = _lexer.next();
            if (!end) return std::unexpected(end.error());
            if (end->type != TokenType::End)
                return fail("Trailing characters after JSON value");
            return value;
        }
    };
} // namespace

std::expected<std::string, std::string> decode_json_string(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\') {
            out += raw[i];
            continue;
        }
        if (++i >= raw.size())
            return std::unexpected("Dangling backslash in string");
        switch (raw[i]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (i + 4 >= raw.size()) // need 4 hex digits after the 'u'
                    return std::unexpected("Truncated \\u escape");
                uint32_t cp = hex4(raw.substr(i + 1, 4));
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) { // high surrogate: must be followed by \uDC00-\uDFFF
                    if (i + 6 >= raw.size() || raw[i + 1] != '\\' || raw[i + 2] != 'u')
                        return std::unexpected("Unpaired UTF-16 surrogate in string");
                    const uint32_t low = hex4(raw.substr(i + 3, 4));
                    if (low < 0xDC00 || low > 0xDFFF)
                        return std::unexpected("Invalid UTF-16 surrogate pair in string");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    i += 6;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return std::unexpected("Unpaired UTF-16 surrogate in string");
                }
                append_utf8(out, cp);
                break;
            }
            default: return std::unexpected("Invalid escape in string");
        }
    }
    return out;
}

std::expected<JsonValue, std::string> parse_json(std::string_view text) {
    auto value = Parser{text}.parse();
    if (!value)
        return std::unexpected(value.error().message());
    return std::move(*value);
}

std::expected<JsonValue, std::string> parse_json(std::span<const std::byte> bytes) {
    return parse_json(std::string_view{reinterpret_cast<const char *>(bytes.data()), bytes.size()});
}
