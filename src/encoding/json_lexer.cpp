#include "json_lexer.hpp"

#include <format>
#include <cctype>

std::expected<std::vector<Token>, std::string> json_lexer(std::span<const std::byte> bytes) {
    std::vector<Token> tokens;
    auto pos{0uz};
    while (pos < bytes.size()) {
        char c = static_cast<char>(bytes[pos]);

        switch (c) {
            case '{':
                tokens.push_back({TokenType::LeftBrace, "{"});
                ++pos;
                break;
            case '}':
                tokens.push_back({TokenType::RightBrace, "}"});
                ++pos;
                break;
            case '[':
                tokens.push_back({TokenType::LeftBracket, "["});
                ++pos;
                break;
            case ']':
                tokens.push_back({TokenType::RightBracket, "]"});
                ++pos;
                break;
            case ':':
                tokens.push_back({TokenType::Colon, ":"});
                ++pos;
                break;
            case ',':
                tokens.push_back({TokenType::Comma, ","});
                ++pos;
                break;
            case '"': {
                // string -- needs special handling
                ++pos;
                const size_t start = pos;
                while (pos < bytes.size() && static_cast<char>(bytes[pos]) != '"')
                    ++pos;
                if (pos >= bytes.size()) {
                    return std::unexpected("Unterminated string");
                }
                std::string value(reinterpret_cast<const char *>(bytes.data() + start), pos - start);
                tokens.push_back({TokenType::String, std::move(value)});
                ++pos;
                break;
            }
            case ' ':
            case '\n':
            case '\r':
            case '\t':
                ++pos;
                break; // skip whitespace
            default: {
                // number, true, false, null
                auto remaining = bytes.subspan(pos);
                auto str = std::string_view(reinterpret_cast<const char *>(remaining.data()), remaining.size());
                if (str.starts_with("true")) {
                    tokens.push_back({TokenType::True, "true"});
                    pos += 4;
                } else if (str.starts_with("false")) {
                    tokens.push_back({TokenType::False, "false"});
                    pos += 5;
                } else if (str.starts_with("null")) {
                    tokens.push_back({TokenType::Null, "null"});
                    pos += 4;
                } else if (std::isdigit(c) || c == '-') {
                    const size_t start = pos;
                    while (pos < bytes.size() &&
                        (std::isdigit(static_cast<char>(bytes[pos])) || static_cast<char>(bytes[pos]) == '.')) {
                        ++pos;
                    }
                        tokens.push_back(
                                {TokenType::Number,
                                 std::string(reinterpret_cast<const char *>(bytes.data() + start), pos - start)});
                }
                else {
                    return std::unexpected(std::format("Unexpected character: {}", c));
                }
                break;
            }
        }
    }
    return tokens;
}
