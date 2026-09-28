#pragma once
#include <cstddef>

static constexpr size_t kReceiveBufferSize = 4096;
static constexpr std::string_view kKalshiHost = "external-api.demo.kalshi.co";
static constexpr std::string_view kOrdersPath = "/trade-api/v2/portfolio/events/orders";