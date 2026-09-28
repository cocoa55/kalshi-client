#pragma once
#include <expected>
#include "json_parser.hpp"
#include "kalshi_auth.hpp"
std::expected<JsonValue, std::string> http_post(std::string_view host, std::string_view path, std::string_view body, const kalshi_auth::Credentials& credentials);
std::expected<JsonValue, std::string> http_get(std::string_view host, std::string_view path, const kalshi_auth::Credentials& credentials);