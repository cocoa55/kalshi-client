#include "strategy.hpp"
#include "test_framework.hpp"

using namespace std::chrono_literals;

namespace {
    MarketState book(const int bid, const int ask) {
        MarketState m;
        (void) m.apply_snapshot(OrderBookSnapshot{.market_ticker = "T", .market_id = "id",
                                                  .yes_dollars_fp = {{std::format("0.{:02}", bid), "10.00"}},
                                                  .no_dollars_fp = {{std::format("0.{:02}", 100 - ask), "10.00"}}});
        return m;
    }

    // Feeds a stable 29/31 book through the warmup period so fair value settles at 30.
    MeanReversionStrategy warmed_up(const Clock::time_point t0) {
        MeanReversionStrategy s{StrategyConfig{}};
        for (int i = 0; i <= 60; i += 5)
            CHECK(!s.on_book_update(book(29, 31), 0, t0 + std::chrono::seconds(i)));
        return s;
    }
    const auto t0 = Clock::time_point{} + 1000s;
    const auto t = t0 + 61s;
} // namespace

TEST(strategy_buys_when_offer_drops_below_fair) {
    auto s = warmed_up(t0);
    const auto signal = s.on_book_update(book(24, 26), 0, t);
    CHECK(signal.has_value());
    CHECK(signal && signal->side == OrderSide::Bid && signal->price == 26);
}

TEST(strategy_sells_when_bid_rises_above_fair) {
    auto s = warmed_up(t0);
    const auto signal = s.on_book_update(book(34, 36), 0, t);
    CHECK(signal && signal->side == OrderSide::Ask && signal->price == 34);
}

TEST(strategy_respects_cooldown_limits_and_spread) {
    auto s = warmed_up(t0);
    CHECK(s.on_book_update(book(24, 26), 0, t).has_value());
    CHECK(!s.on_book_update(book(24, 26), 0, t + 1s));  // cooldown
    CHECK(!s.on_book_update(book(24, 26), 5, t + 10s)); // at max long position
    CHECK(!s.on_book_update(book(10, 26), 0, t + 20s)); // spread too wide
    auto s2 = warmed_up(t0);
    CHECK(!s2.on_book_update(book(34, 36), -5, t));     // at max short position
}

TEST(strategy_does_not_trade_during_warmup_or_on_bad_books) {
    MeanReversionStrategy s{StrategyConfig{}};
    CHECK(!s.on_book_update(book(29, 31), 0, t0));
    CHECK(!s.on_book_update(book(10, 12), 0, t0 + 10s)); // big move but still warming up
    MarketState one_sided;
    (void) one_sided.apply_snapshot(OrderBookSnapshot{.market_ticker = "T", .market_id = "id",
                                                      .yes_dollars_fp = {{"0.30", "1.00"}}});
    CHECK(!s.on_book_update(one_sided, 0, t0 + 120s));
}

TEST(strategy_builds_ioc_order) {
    const auto order = make_ioc_order("T", Signal{.side = OrderSide::Ask, .price = 7, .count = 2}, "c1");
    CHECK_EQ(order.price, std::string{"0.07"});
    CHECK_EQ(order.count, std::string{"2.00"});
    CHECK(order.time_in_force == TimeInForce::ImmediateOrCancel);
}
