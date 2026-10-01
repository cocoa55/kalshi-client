#include "json_lexer.hpp"

#include <format>

namespace {
    constexpr bool is_digit(const char c) { return c >= '0' && c <= '9'; }
    constexpr bool is_hex(const char c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
} // namespace

std::string JsonError::message() const { return std::format("{} at offset {}", what, offset); }

std::expected<Token, JsonError> JsonLexer::next() {
    while (_pos < _input.size()) {
        const char c = _input[_pos];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
            break;
        ++_pos;
    }
    if (_pos >= _input.size())
        return Token{.type = TokenType::End};

    const char c = _input[_pos];
    auto single = [&](TokenType type) {
        return Token{.type = type, .text = _input.substr(_pos++, 1)};
    };
    switch (c) {
        case '{': return single(TokenType::LeftBrace);
        case '}': return single(TokenType::RightBrace);
        case '[': return single(TokenType::LeftBracket);
        case ']': return single(TokenType::RightBracket);
        case ':': return single(TokenType::Colon);
        case ',': return single(TokenType::Comma);
        case '"': return lex_string();
        default: break;
    }

    const auto rest = _input.substr(_pos);
    for (const auto &[word, type]: {std::pair{std::string_view{"true"}, TokenType::True},
                                   std::pair{std::string_view{"false"}, TokenType::False},
                                   std::pair{std::string_view{"null"}, TokenType::Null}}) {
        if (rest.starts_with(word)) {
            _pos += word.size();
            return Token{.type = type, .text = word};
        }
    }
    if (c == '-' || is_digit(c))
        return lex_number();

    return std::unexpected(JsonError{"Unexpected character", _pos});
}

std::expected<Token, JsonError> JsonLexer::lex_string() {
    const size_t start = ++_pos; // skip opening quote
    bool has_escapes = false;
    while (_pos < _input.size()) {
        const auto c = static_cast<unsigned char>(_input[_pos]);
        if (c == '"') {
            Token token{.type = TokenType::String, .text = _input.substr(start, _pos - start), .has_escapes = has_escapes};
            ++_pos;
            return token;
        }
        if (c < 0x20)
            return std::unexpected(JsonError{"Unescaped control character in string", _pos});
        if (c == '\\') {
            has_escapes = true;
            if (++_pos >= _input.size())
                break;
            switch (_input[_pos]) {
                case '"': case '\\': case '/': case 'b': case 'f': case 'n': case 'r': case 't':
                    break;
                case 'u':
                    for (int i = 0; i < 4; ++i)
                        if (++_pos >= _input.size() || !is_hex(_input[_pos]))
                            return std::unexpected(JsonError{"Invalid \\u escape in string", _pos});
                    break;
                default:
                    return std::unexpected(JsonError{"Invalid escape in string", _pos});
            }
        }
        ++_pos;
    }
    return std::unexpected(JsonError{"Unterminated string", start - 1});
}

// JSON number grammar: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
std::expected<Token, JsonError> JsonLexer::lex_number() {
    const size_t start = _pos;
    auto peek = [&] { return _pos < _input.size() ? _input[_pos] : '\0'; };
    auto digits = [&] {
        const size_t before = _pos;
        while (is_digit(peek()))
            ++_pos;
        return _pos > before;
    };

    if (peek() == '-')
        ++_pos;
    if (peek() == '0')
        ++_pos;
    else if (!digits())
        return std::unexpected(JsonError{"Invalid number", start});

    if (peek() == '.') {
        ++_pos;
        if (!digits())
            return std::unexpected(JsonError{"Invalid number: no digits after '.'", start});
    }
    if (peek() == 'e' || peek() == 'E') {
        ++_pos;
        if (peek() == '+' || peek() == '-')
            ++_pos;
        if (!digits())
            return std::unexpected(JsonError{"Invalid number: bad exponent", start});
    }
    return Token{.type = TokenType::Number, .text = _input.substr(start, _pos - start)};
}
