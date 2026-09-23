#pragma once
#include <string>

struct ParseError {
    enum class Kind {
        Incomplete,
        Malformed
    };

    Kind kind{Kind::Malformed};
    std::string message{};
};