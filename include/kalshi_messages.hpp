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
    const uint64_t ts_ms {};
};

enum class FillAction {
    Buy,
    Sell
};

struct Fill {
    const std::string order_id;
    const std::string market_ticker;
    const std::string count_fp;
    const FillAction action;
};



struct Message {
    const std::string type;
    const uint32_t sid{};
    const uint32_t seq{};
    std::variant<std::monostate, OrderBookSnapshot, OrderBookDelta, Fill> msg;
};

enum class OrderSide { Bid, Ask };
enum class TimeInForce { FillOrKill, GoodTillCanceled, ImmediateOrCancel };
enum class SelfTradePrevention { TakerAtCross, Maker };

struct OrderRequest {
    std::string ticker;
    OrderSide side;
    std::string count;
    std::string price;
    TimeInForce time_in_force;
    SelfTradePrevention self_trade_prevention;
    std::string client_order_id;
    bool post_only{false};
};

struct OrderResponse {
    std::string order_id;
    std::string fill_count;
    std::string remaining_count;
    uint64_t ts_ms;
    std::string client_order_id;
};

std::expected<Message, std::string> parse_kalshi_message(const JsonValue &json);
std::string serialize_order_request(const OrderRequest& order);
std::expected<OrderResponse, std::string> parse_order_response(const JsonValue& json);