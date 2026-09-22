#pragma once
#include <array>
#include <string>
#include <openssl/err.h>

inline std::string ssl_error_string() {
    const unsigned long err = ERR_get_error();
    std::array<char, 256> buf{};
    ERR_error_string_n(err, buf.data(), buf.size());
    return std::string{buf.data()};
}