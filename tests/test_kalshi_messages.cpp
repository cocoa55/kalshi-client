#include "json_parser.hpp"
#include "kalshi_messages.hpp"
#include "message_compare.hpp"
#include "test_framework.hpp"

namespace {
    std::span<const std::byte> bytes(const std::string_view s) { return std::as_bytes(std::span(s)); }

    // Decodes with both the direct decoder and the JSON-tree path and checks they agree.
    std::expected<Message, std::string> decode_both(const std::string_view payload) {
        auto direct = decode_kalshi_message(bytes(payload));
        auto json = parse_json(payload);
        auto via_tree = json ? parse_kalshi_message(*json) : std::unexpected(json.error());
        CHECK_EQ(direct.has_value(), via_tree.has_value());
        if (direct && via_tree)
            CHECK(same_message(*direct, *via_tree));
        return direct;
    }
} // namespace

TEST(decode_orderbook_delta) {
    auto m = decode_both(R"({"type":"orderbook_delta","sid":7,"seq":42,"msg":{"market_ticker":"KX-T","market_id":"m1",)"
                         R"("price_dollars":"0.5900","delta_fp":"-12.00","side":"yes","ts":"2026-09-30T00:00:00Z","ts_ms":1790819126123}})");
    CHECK(m.has_value());
    if (!m) return;
    CHECK_EQ(m->sid, 7u);
    CHECK_EQ(m->seq, 42u);
    const auto &d = std::get<OrderBookDelta>(m->msg);
    CHECK_EQ(d.price_dollars, std::string{"0.5900"});
    CHECK_EQ(d.ts_ms, uint64_t{1790819126123});
}

TEST(decode_orderbook_snapshot) {
    auto m = decode_both(R"({"type":"orderbook_snapshot","sid":7,"seq":1,"msg":{"market_ticker":"KX-T","market_id":"m1",)"
                         R"("yes_dollars_fp":[["0.4000","10.00"],["0.4500","3.00"]],"no_dollars_fp":[["0.5000","7.00"]]}})");
    CHECK(m.has_value());
    if (!m) return;
    const auto &s = std::get<OrderBookSnapshot>(m->msg);
    CHECK_EQ(s.yes_dollars_fp.size(), size_t{2});
    CHECK_EQ(s.no_dollars_fp[0].first, std::string{"0.5000"});
}

TEST(decode_snapshot_with_missing_sides) {
    auto m = decode_both(R"({"type":"orderbook_snapshot","msg":{"market_ticker":"KX-T","market_id":"m1"}})");
    CHECK(m && std::get<OrderBookSnapshot>(m->msg).yes_dollars_fp.empty());
}

TEST(decode_fill_and_subscribed) {
    auto fill = decode_both(R"({"type":"fill","sid":3,"msg":{"order_id":"o1","market_ticker":"KX-T","count_fp":"1.00","action":"sell"}})");
    CHECK(fill && std::get<Fill>(fill->msg).action == FillAction::Sell);
    auto sub = decode_both(R"({"type":"subscribed","id":1,"msg":{"channel":"orderbook_delta","sid":7}})");
    CHECK(sub && std::holds_alternative<std::monostate>(sub->msg));
}

TEST(decode_rejects_bad_messages_the_same_way) {
    for (const char *bad: {
                 R"([])",                                                          // root not an object
                 R"({"msg":{}})",                                                  // no type
                 R"({"type":"orderbook_delta"})",                                 // no msg
                 R"({"type":"orderbook_delta","msg":{"market_ticker":"T"}})",     // missing fields
                 R"({"type":"nope","msg":{}})",                                   // unknown type
                 R"({"type":"fill","msg":{"order_id":"o","market_ticker":"T","count_fp":"1","action":"hold"}})",
                 R"({"type":"orderbook_snapshot","msg":{"market_ticker":"T","market_id":"m","yes_dollars_fp":[["0.1"]]}})",
                 R"({"type":"orderbook_snapshot","msg":{"market_ticker":"T","market_id":"m","no_dollars_fp":[["0.1",["x"]]]}})",
                 R"({"type":"orderbook_delta","msg":{"market_ticker":"T","market_id":"m","price_dollars":"0.5","delta_fp":"1","side":"yes","ts":"t","ts_ms":"abc"}})",
                 R"({"type":"subscribed"} trailing)",
         })
        CHECK(!decode_both(bad).has_value());
}

TEST(decode_matches_tree_on_edge_cases) {
    // Duplicate keys (first wins), wrong-typed values, numbers as strings, unknown nested junk.
    for (const char *edge: {
                 R"({"type":"fill","type":"orderbook_delta","msg":{"order_id":"o","market_ticker":"T","count_fp":"1","action":"buy"}})",
                 R"({"type":"fill","msg":{"order_id":"o","order_id":"p","market_ticker":"T","count_fp":2,"action":"buy","x":{"y":[1,{"z":null}]}}})",
                 R"({"type":"fill","msg":"not an object","msg":{"order_id":"o","market_ticker":"T","count_fp":"1","action":"buy"}})",
                 R"({"type":"orderbook_snapshot","msg":{"market_ticker":"T","market_id":"m","yes_dollars_fp":{"not":"array"}}})",
                 R"({"type":"orderbook_snapshot","msg":{"market_ticker":"T","market_id":"m","yes_dollars_fp":[[1,2]],"yes_dollars_fp":[["bad"]]}})",
                 R"({"type":"orderbook_delta","sid":"9","seq":-1,"msg":{"market_ticker":"T","market_id":"m","price_dollars":"0.5","delta_fp":"1","side":"yes","ts":"t","ts_ms":"5"}})",
                 R"({"type":true,"msg":{}})",
         })
        (void) decode_both(edge);
}

TEST(decode_falls_back_for_escaped_strings) {
    // "typ\u0065" is the key "type"; the direct decoder can't compare escaped keys, so it must defer to the tree.
    auto m = decode_both(R"({"typ\u0065":"fill","msg":{"order_id":"o\"1","market_ticker":"T","count_fp":"1","action":"buy"}})");
    CHECK(m && std::get<Fill>(m->msg).order_id == "o\"1");
}

TEST(order_request_serializes) {
    const OrderRequest order{.ticker = "KX-T", .side = OrderSide::Bid, .count = "1.00", .price = "0.42",
                             .time_in_force = TimeInForce::ImmediateOrCancel,
                             .self_trade_prevention = SelfTradePrevention::TakerAtCross,
                             .client_order_id = "c1", .post_only = false};
    auto json = parse_json(serialize_order_request(order));
    CHECK(json.has_value());
    if (!json) return;
    const auto &o = std::get<JsonObject>(json->data);
    CHECK_EQ(std::get<std::string>(o.at("side").data), std::string{"bid"});
    CHECK_EQ(std::get<std::string>(o.at("time_in_force").data), std::string{"immediate_or_cancel"});
}

TEST(order_response_parses_and_reports_errors) {
    auto ok = parse_order_response(*parse_json(
            R"({"order_id":"o1","fill_count":"1.00","remaining_count":"0.00","ts_ms":"1790819126123","client_order_id":"c1"})"));
    CHECK(ok && ok->order_id == "o1" && ok->fill_count == "1.00");
    auto err = parse_order_response(*parse_json(R"({"error":{"code":"insufficient_shard_balance","message":"x"}})"));
    CHECK(!err.has_value());
    CHECK(!err && err.error().contains("insufficient_shard_balance"));
}

TEST(to_json_string_round_trips) {
    const std::string tricky = R"({"a":"quote \" backslash \\ newline \n","b":[true,null,{"c":"\u0001"}]})";
    auto first = parse_json(tricky);
    CHECK(first.has_value());
    if (!first) return;
    const std::string dumped = to_json_string(*first);
    auto second = parse_json(dumped);
    CHECK(second.has_value());
    if (second) CHECK_EQ(to_json_string(*second), dumped);
}
