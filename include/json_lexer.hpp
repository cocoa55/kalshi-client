#pragma once
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

enum class TokenType {
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Colon,
    Comma,
    String,
    Number,
    True,
    False,
    Null,
    End
};

// Lightweight error: a static message plus the byte offset where it occurred. Keeping std::string out of the
// per-token return type makes std::expected<Token, JsonError> trivially copyable; measured on a 1.8 KB
// snapshot, that alone made lexing ~37% faster. Callers turn it into a std::string only if it's reported.
struct JsonError {
    const char *what;
    size_t offset;
    std::string message() const;
};

// A token is a view into the input buffer: lexing allocates nothing. For strings, `text` is the raw content
// between the quotes with escape sequences still encoded; `has_escapes` tells the parser whether it needs
// to decode them (most Kalshi strings have none, so they can be copied straight out).
struct Token {
    TokenType type{TokenType::End};
    std::string_view text{};
    bool has_escapes{false};
};

// Pull-based lexer: the parser asks for one token at a time, so no token array is ever materialized.
// The input must outlive the lexer and every token it returns.
class JsonLexer {
    std::string_view _input;
    size_t _pos{0};

    std::expected<Token, JsonError> lex_string();
    std::expected<Token, JsonError> lex_number();

public:
    explicit JsonLexer(std::span<const std::byte> bytes)
        : _input(reinterpret_cast<const char *>(bytes.data()), bytes.size()) {}
    explicit JsonLexer(std::string_view text) : _input(text) {}

    std::expected<Token, JsonError> next();
    size_t position() const { return _pos; }
};
