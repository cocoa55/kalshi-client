#include "kalshi_messages.hpp"
#include <functional>
#include <optional>

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

    std::optional<int64_t> get_int64(const JsonObject &obj, const std::string &key) {
        auto it = obj.find(key);
        if (it == obj.end())
            return std::nullopt;
        auto res = std::get_if<std::string>(&it->second.data);
        if (!res)
            return std::nullopt;
        try {
            return std::stoll(*res);
        } catch (...) {
            return std::nullopt;
        }
    }

    std::expected<OrderBookDelta, std::string> parse_delta(const JsonObject &msg_obj) {
        auto ticker = get_string(msg_obj, "market_ticker");
        auto market_id = get_string(msg_obj, "market_id");
        auto price_dollars = get_string(msg_obj, "price_dollars");
        auto delta_fp = get_string(msg_obj, "delta_fp");
        auto side = get_string(msg_obj, "side");
        auto ts = get_string(msg_obj, "ts");
        auto ts_ms = get_int64(msg_obj, "ts_ms");

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
} // namespace


std::expected<Message, std::string> parse_kalshi_message(const JsonValue &json) {

    auto root_obj_ptr = std::get_if<JsonObject>(&json.data);
    if (!root_obj_ptr)
        return std::unexpected("Root JSON is not an object");

    const JsonObject &root_obj = *root_obj_ptr;

    const auto msg_type = get_string(root_obj, "type");
    if (!msg_type)
        return std::unexpected("Missing or invalid 'type' field");

    auto msg_obj_ptr = get_object(root_obj, "msg");
    if (!msg_obj_ptr)
        return std::unexpected("Missing 'msg' object in Kalshi message");

    const JsonObject &msg_obj = msg_obj_ptr->get();

    if (*msg_type == "orderbook_delta") {
        auto delta_result = parse_delta(msg_obj);
        if (!delta_result)
            return std::unexpected(delta_result.error());

        return Message{.type = *msg_type, .sid = 0, .seq = 0, .msg = std::move(delta_result.value())};

    } else if (*msg_type == "orderbook_snapshot") {
        auto snapshot_result = parse_snapshot(msg_obj);
        if (!snapshot_result)
            return std::unexpected(snapshot_result.error());

        return Message{.type = *msg_type, .sid = 0, .seq = 0, .msg = std::move(snapshot_result.value())};
    } else if (*msg_type == "subscribed") {
        return Message {
            .type = *msg_type,
            .sid = 0,
            .seq = 0,
            .msg = std::monostate{}
        };
    } else {
        return std::unexpected("Unknown message type: " + *msg_type);
    }
}
