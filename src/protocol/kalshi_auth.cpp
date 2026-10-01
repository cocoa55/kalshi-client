#include "kalshi_auth.hpp"
#include "openssl_util.hpp"
#include <array>
#include <cstdlib>
#include <memory>
#include <format>
#include <vector>
#include "time_util.hpp"
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/err.h>




namespace {
    std::string base64_encode(const std::vector<unsigned char>& data) {
        std::string encoded;
        // EVP_EncodeBlock writes 4 output bytes per 3 input bytes plus a NUL; encode directly into the string.
        encoded.resize_and_overwrite(4 * ((data.size() + 2) / 3) + 1, [&](char *buf, size_t) {
            return static_cast<size_t>(EVP_EncodeBlock(reinterpret_cast<unsigned char *>(buf), data.data(),
                                                       static_cast<int>(data.size())));
        });
        return encoded;
    }
}


namespace kalshi_auth {

std::expected<Credentials, std::string> load_credentials_from_env() {
    const char* key_id = std::getenv("KALSHI_API_KEY_ID");
    const char* key_path = std::getenv("KALSHI_PRIVATE_KEY_PATH");

    if (!key_id || !*key_id) {
        return std::unexpected("KALSHI_API_KEY_ID environment variable is not set");
    }
    if (!key_path || !*key_path) {
        return std::unexpected("KALSHI_PRIVATE_KEY_PATH environment variable is not set");
    }

    return Credentials{.key_id = key_id, .private_key_path = key_path};
}

std::expected<Signer, std::string> Signer::from_credentials(const Credentials &credentials) {
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_file(credentials.private_key_path.c_str(), "r"), BIO_free);
    if (!bio) {
        return std::unexpected("Failed to open private key file: " + credentials.private_key_path);
    }
    EVP_PKEY *key = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
    if (!key) {
        return std::unexpected("Failed to parse private key: " + ssl_error_string());
    }
    return Signer{credentials.key_id, key};
}

std::expected<std::string, std::string> Signer::auth_headers(std::string_view method, std::string_view path) const {
    const std::string timestamp = current_timestamp_ms();
    auto signature = sign(std::format("{}{}{}", timestamp, method, path));
    if (!signature) {
        return std::unexpected("Failed to sign request: " + signature.error());
    }
    return std::format("KALSHI-ACCESS-KEY: {}\r\n"
                       "KALSHI-ACCESS-SIGNATURE: {}\r\n"
                       "KALSHI-ACCESS-TIMESTAMP: {}\r\n",
                       _key_id, *signature, timestamp);
}

std::expected<std::string, std::string> Signer::sign(std::string_view message) const {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> mdctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!mdctx) {
        return std::unexpected("Failed to create digest context");
    }

    EVP_PKEY_CTX* pctx = nullptr;
    if (EVP_DigestSignInit(mdctx.get(), &pctx, EVP_sha256(), nullptr, _key.get()) != 1) {
        return std::unexpected("EVP_DigestSignInit failed: " + ssl_error_string());
    }

    if (EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) <= 0) {
        return std::unexpected("Failed to set RSA-PSS padding: " + ssl_error_string());
    }
    if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, RSA_PSS_SALTLEN_DIGEST) <= 0) {
        return std::unexpected("Failed to set RSA-PSS salt length: " + ssl_error_string());
    }

    if (EVP_DigestSignUpdate(mdctx.get(), message.data(), message.size()) != 1) {
        return std::unexpected("EVP_DigestSignUpdate failed: " + ssl_error_string());
    }

    size_t sig_len = 0;
    if (EVP_DigestSignFinal(mdctx.get(), nullptr, &sig_len) != 1) {
        return std::unexpected("Failed to determine signature length: " + ssl_error_string());
    }

    std::vector<unsigned char> signature(sig_len);
    if (EVP_DigestSignFinal(mdctx.get(), signature.data(), &sig_len) != 1) {
        return std::unexpected("EVP_DigestSignFinal failed: " + ssl_error_string());
    }
    signature.resize(sig_len);

    return base64_encode(signature);
}

}
