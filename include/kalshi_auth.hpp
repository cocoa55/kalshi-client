#pragma once
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <openssl/evp.h>

namespace kalshi_auth {

struct Credentials {
    std::string key_id;
    std::string private_key_path;
};

// Reads KALSHI_API_KEY_ID and KALSHI_PRIVATE_KEY_PATH from the environment.
[[nodiscard]] std::expected<Credentials, std::string> load_credentials_from_env();

// Holds the parsed RSA private key for the lifetime of the process. Loading and parsing the PEM file
// once at startup keeps file I/O and ASN.1 decoding off the order path; each request only pays for
// the RSA-PSS signature itself.
class Signer {
    struct PkeyDeleter {
        static void operator()(EVP_PKEY *key) noexcept { EVP_PKEY_free(key); }
    };
    std::string _key_id;
    std::unique_ptr<EVP_PKEY, PkeyDeleter> _key;

    Signer(std::string key_id, EVP_PKEY *key) : _key_id(std::move(key_id)), _key(key) {}

public:
    [[nodiscard]] static std::expected<Signer, std::string> from_credentials(const Credentials &credentials);

    // RSA-PSS/SHA-256 (salt length == digest length), base64-encoded, as Kalshi expects.
    [[nodiscard]] std::expected<std::string, std::string> sign(std::string_view message) const;

    // The three KALSHI-ACCESS-* header lines (each ending in \r\n) for a request, signed over
    // timestamp + method + path.
    [[nodiscard]] std::expected<std::string, std::string> auth_headers(std::string_view method,
                                                                      std::string_view path) const;
};

}
