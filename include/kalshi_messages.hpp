#pragma once
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <variant>
#include <vector>


struct JsonValue;
using PriceLevel = std::pair<std::string, std::string>;

// Plain (non-const) members so these can be moved, not copied, into Message.
struct OrderBookSnapshot {
    std::string market_ticker;
    std::string market_id;
    std::vector<PriceLevel> yes_dollars_fp;
    std::vector<PriceLevel> no_dollars_fp;
};

struct OrderBookDelta {
    std::string market_ticker;
    std::string market_id;
    std::string price_dollars;
    std::string delta_fp;
    std::string side;
    std::string ts;
    uint64_t ts_ms {};
};

enum class FillAction {
    Buy,
    Sell
};

struct Fill {
    std::string order_id;
    std::string market_ticker;
    std::string count_fp;
    FillAction action;
};



struct Message {
    std::string type;
    uint32_t sid{};
    uint32_t seq{};
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

// Decodes a WebSocket text payload directly from the token stream into typed structs, without building
// an intermediate JSON tree. Semantically identical to parse_kalshi_message(parse_json(bytes)) (verified by
// a differential fuzzer); falls back to that path for the rare payload containing escaped strings.
std::expected<Message, std::string> decode_kalshi_message(std::span<const std::byte> bytes);
// Tree-based decoding of an already-parsed JSON value.
std::expected<Message, std::string> parse_kalshi_message(const JsonValue &json);
std::string serialize_order_request(const OrderRequest& order);
std::string to_json_string(const JsonValue &value);
std::expected<OrderResponse, std::string> parse_order_response(const JsonValue& json);