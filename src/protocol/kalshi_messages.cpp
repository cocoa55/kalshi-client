#include "kalshi_messages.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <functional>
#include <optional>
#include <ranges>

#include "json_parser.hpp"
namespace {
    std::optional<std::string> get_string(const JsonObject &obj, const std::string &key) {
        auto it = obj.find(key);
        if (it == obj.end())
            return std::nullopt;

        auto res = std::get_if<std::string>(&it->second.data);
        if (res) {
            return *res;
        }
        return std::nullopt;
    }
    std::string_view to_string(const OrderSide side) {
            switch (side) {
                case OrderSide::Bid: return "bid";
                case OrderSide::Ask: return "ask";
                default: std::unreachable();
            }
    }
    std::string_view to_string(const TimeInForce tif) {
            switch (tif) {
                case TimeInForce::FillOrKill: return "fill_or_kill";
                case TimeInForce::GoodTillCanceled: return "good_till_canceled";
                case TimeInForce::ImmediateOrCancel: return "immediate_or_cancel";
                default: std::unreachable();
            }
    }
    std::string_view to_string(const SelfTradePrevention stp) {
            switch (stp) {
                case SelfTradePrevention::Maker: return "maker";
                case SelfTradePrevention::TakerAtCross: return "taker_at_cross";
                default: std::unreachable();
            }
    }

    std::optional<std::reference_wrapper<const JsonObject>> get_object(const JsonObject &obj, const std::string &key) {
        auto it = obj.find(key);
        if (it == obj.end())
            return std::nullopt;

        auto res = std::get_if<JsonObject>(&it->second.data);
        if (res)
            return std::cref(*res);

        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const JsonArray>> get_array(const JsonObject &obj, const std::string &key) {
        auto it = obj.find(key);
        if (it == obj.end())
            return std::nullopt;

        auto res = std::get_if<JsonArray>(&it->second.data);
        if (res)
            return std::cref(*res);

        return std::nullopt;
    }

    std::optional<uint64_t> get_uint64(const JsonObject &obj, const std::string &key) {
        auto it = obj.find(key);
        if (it == obj.end())
            return std::nullopt;
        auto res = std::get_if<std::string>(&it->second.data);
        if (!res)
            return std::nullopt;
        uint64_t value{};
        const auto [end, ec] = std::from_chars(res->data(), res->data() + res->size(), value);
        if (ec != std::errc{} || end != res->data() + res->size())
            return std::nullopt;
        return value;
    }

    std::expected<OrderBookDelta, std::string> parse_delta(const JsonObject &msg_obj) {
        auto ticker = get_string(msg_obj, "market_ticker");
        auto market_id = get_string(msg_obj, "market_id");
        auto price_dollars = get_string(msg_obj, "price_dollars");
        auto delta_fp = get_string(msg_obj, "delta_fp");
        auto side = get_string(msg_obj, "side");
        auto ts = get_string(msg_obj, "ts");
        auto ts_ms = get_uint64(msg_obj, "ts_ms");

        if (!ticker || !market_id || !price_dollars || !delta_fp || !side || !ts || !ts_ms) {
            return std::unexpected("Invalid Key");
        }

        return OrderBookDelta{.market_ticker = *ticker,
                              .market_id = *market_id,
                              .price_dollars = *price_dollars,
                              .delta_fp = *delta_fp,
                              .side = *side,
                              .ts = *ts,
                              .ts_ms = *ts_ms};
    }

    std::expected<std::vector<PriceLevel>, std::string> parse_price_levels(const JsonArray &json_array) {
        std::vector<PriceLevel> levels;
        for (const auto &item: json_array) {
            auto arr = std::get_if<JsonArray>(&item.data);
            if (!arr || arr->size() != 2)
                return std::unexpected("Price level is not a 2-element array");

            const auto &val1 = (*arr)[0];
            const auto &val2 = (*arr)[1];

            auto str1 = std::get_if<std::string>(&val1.data);
            auto str2 = std::get_if<std::string>(&val2.data);

            if (!str1 || !str2)
                return std::unexpected("Price levels contain non-string data");

            levels.emplace_back(*str1, *str2);
        }
        return levels;
    }

    std::expected<OrderBookSnapshot, std::string> parse_snapshot(const JsonObject &msg_obj) {
        auto ticker = get_string(msg_obj, "market_ticker");
        auto market_id = get_string(msg_obj, "market_id");

        if (!ticker || !market_id ) {
            return std::unexpected("Missing required fields in snapshot");
        }
        std::vector<PriceLevel> yes_levels;
        std::vector<PriceLevel> no_levels;

        auto yes_arr = get_array(msg_obj, "yes_dollars_fp");
        auto no_arr = get_array(msg_obj, "no_dollars_fp");

        if (yes_arr) {
            auto result = parse_price_levels(yes_arr->get());
            if (!result) return std::unexpected(result.error());
            yes_levels = std::move(result.value());
        }
        if (no_arr) {
            auto result = parse_price_levels(no_arr->get());
            if (!result) return std::unexpected(result.error());
            no_levels = std::move(result.value());
        }

        return OrderBookSnapshot{.market_ticker = *ticker,
                                 .market_id = *market_id,
                                 .yes_dollars_fp = std::move(yes_levels),
                                 .no_dollars_fp = std::move(no_levels)};
    }
    std::expected<Fill, std::string> parse_fill(const JsonObject &msg_obj) {
        const auto order_id = get_string(msg_obj, "order_id");
        const auto market_ticker = get_string(msg_obj, "market_ticker");
        const auto count_fp = get_string(msg_obj, "count_fp");

        auto action_str = get_string(msg_obj, "action");
        if (!action_str)
            return std::unexpected("Missing action field");

        FillAction action;
        if (*action_str == "buy") action = FillAction::Buy;
        else if (*action_str == "sell") action = FillAction::Sell;
        else return std::unexpected("Unknown action: " + *action_str);

        if (!order_id || !market_ticker || !count_fp)
            return std::unexpected("Missing fields in order response");


        return Fill{
            .order_id = *order_id,
            .market_ticker = *market_ticker,
            .count_fp = *count_fp,
            .action = action
        };
    }
} // namespace

namespace {
    // Quotes and escapes a string so the output is always valid JSON.
    std::string json_quote(const std::string_view s) {
        std::string out;
        out.reserve(s.size() + 2);
        out += '"';
        for (const char c: s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                        out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
                    else
                        out += c;
            }
        }
        out += '"';
        return out;
    }
} // namespace

// Numbers are stored as text by the parser, so they come back quoted; good enough for diagnostics.
std::string to_json_string(const JsonValue &value) {
    return std::visit(
            [](const auto &v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    return "null";
                else if constexpr (std::is_same_v<T, bool>)
                    return v ? "true" : "false";
                else if constexpr (std::is_same_v<T, std::string>)
                    return json_quote(v);
                else if constexpr (std::is_same_v<T, JsonArray>) {
                    std::string out = "[";
                    for (const auto &[i, element]: std::views::enumerate(v))
                        out += (i ? "," : "") + to_json_string(element);
                    return out + "]";
                } else {
                    std::string out = "{";
                    for (const auto &[i, member]: std::views::enumerate(v))
                        out += std::format("{}{}:{}", i ? "," : "", json_quote(member.first), to_json_string(member.second));
                    return out + "}";
                }
            },
            value.data);
}


std::expected<Message, std::string> parse_kalshi_message(const JsonValue &json) {
    auto root_obj_ptr = std::get_if<JsonObject>(&json.data);
    if (!root_obj_ptr)
        return std::unexpected("Root JSON is not an object");
    const JsonObject &root_obj = *root_obj_ptr;

    const auto msg_type = get_string(root_obj, "type");
    if (!msg_type)
        return std::unexpected("Missing or invalid 'type' field");

    // sid identifies the subscription, seq orders its messages; used to detect dropped deltas.
    const auto sid = static_cast<uint32_t>(get_uint64(root_obj, "sid").value_or(0));
    const auto seq = static_cast<uint32_t>(get_uint64(root_obj, "seq").value_or(0));
    auto make = [&](auto &&payload) {
        return Message{.type = *msg_type, .sid = sid, .seq = seq, .msg = std::forward<decltype(payload)>(payload)};
    };

    if (*msg_type == "subscribed")
        return make(std::monostate{});

    auto msg_obj_ptr = get_object(root_obj, "msg");
    if (!msg_obj_ptr)
        return std::unexpected(std::format("Missing 'msg' object in '{}' message", *msg_type));
    const JsonObject &msg_obj = msg_obj_ptr->get();

    if (*msg_type == "orderbook_delta")
        return parse_delta(msg_obj).transform(make);
    if (*msg_type == "orderbook_snapshot")
        return parse_snapshot(msg_obj).transform(make);
    if (*msg_type == "fill")
        return parse_fill(msg_obj).transform(make);
    return std::unexpected("Unknown message type: " + *msg_type);
}

std::expected<OrderResponse, std::string> parse_order_response(const JsonValue& json) {
    auto root_obj_ptr = std::get_if<JsonObject>(&json.data);
    if (!root_obj_ptr) return std::unexpected("Root JSON is not an object");
    const JsonObject &root_obj = *root_obj_ptr;

    auto order_id = get_string(root_obj, "order_id");
    auto fill_count = get_string(root_obj, "fill_count");
    auto remaining_count = get_string(root_obj, "remaining_count");
    auto ts_ms = get_uint64(root_obj, "ts_ms");
    auto client_order_id = get_string(root_obj, "client_order_id");

    if (!order_id || !fill_count || !remaining_count || !ts_ms) {
        std::string body = to_json_string(json);
        if (body.size() > 1000)
            body = body.substr(0, 1000) + "...";
        return std::unexpected("Unexpected order response: " + body);
    }


    return OrderResponse {
        .order_id = std::move(*order_id),
        .fill_count = std::move(*fill_count),
        .remaining_count = std::move(*remaining_count),
        .ts_ms = *ts_ms,
        .client_order_id = client_order_id ? std::move(*client_order_id) : std::string{}

    };
}


std::string serialize_order_request(const OrderRequest &order) {
    return std::format(
    R"({{"ticker":"{}","side":"{}","count":"{}","price":"{}","time_in_force":"{}","self_trade_prevention_type":"{}","post_only":{},"client_order_id":"{}"}})",
    order.ticker,
    to_string(order.side),
    order.count,
    order.price,
    to_string(order.time_in_force),
    to_string(order.self_trade_prevention),
    order.post_only ? "true" : "false",
    order.client_order_id
    );
}