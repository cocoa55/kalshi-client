#pragma once
#include <cstdint>
#include <expected>
#include <string>
#include <variant>
#include <vector>


struct JsonValue;
using PriceLevel = std::pair<std::string, std::string>;

struct OrderBookSnapshot {
    const std::string market_ticker;
    const std::string market_id;
    const std::vector<PriceLevel> yes_dollars_fp;
    const std::vector<PriceLevel> no_dollars_fp;
};



struct OrderBookDelta {
    const std::string market_ticker;
    const std::string market_id;
    const std::string price_dollars;
    const std::string delta_fp;
    const std::string side;
    const std::string ts;
    const int64_t ts_ms {};
};


struct Message {
    const std::string type;
    const uint32_t sid{};
    const uint32_t seq{};
    std::variant<std::monostate, OrderBookSnapshot, OrderBookDelta> msg;
};

std::expected<Message, std::string> parse_kalshi_message(const JsonValue &json);
