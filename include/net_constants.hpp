#pragma once
#include <cstddef>
#include <string_view>

static constexpr size_t kReceiveBufferSize = 4096;
static constexpr long kReceiveTimeoutSeconds = 30;
static constexpr std::string_view kKalshiWsHost = "external-api-ws.demo.kalshi.co";
static constexpr std::string_view kKalshiHost = "external-api.demo.kalshi.co";
static constexpr std::string_view kOrdersPath = "/trade-api/v2/portfolio/events/orders";static constexpr std::string_view kBalancePath = "/trade-api/v2/portfolio/balance";
static constexpr std::string_view kShardTransferPath = "/trade-api/v2/portfolio/intra_exchange_instance_transfer";
