#pragma once
#include <expected>
#include <span>
#include <string>
#include <vector>

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
    Null
};

struct Token {
    TokenType type;
    std::string value;
};

std::expected<std::vector<Token>, std::string> json_lexer(std::span<const std::byte> bytes);


