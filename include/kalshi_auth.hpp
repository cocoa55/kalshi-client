#pragma once
#include <expected>
#include <string>

namespace kalshi_auth {

struct Credentials {
    std::string key_id;
    std::string private_key_path;
};

// Reads KALSHI_API_KEY_ID and KALSHI_PRIVATE_KEY_PATH from the environment.
[[nodiscard]] std::expected<Credentials, std::string> load_credentials_from_env();

// Signs `message` with the RSA private key at `private_key_path` using
// RSA-PSS/SHA-256 (salt length == digest length), returning the base64-encoded
// signature Kalshi expects in the KALSHI-ACCESS-SIGNATURE header.
[[nodiscard]] std::expected<std::string, std::string> sign(
    const std::string& private_key_path,
    const std::string& message);

}
