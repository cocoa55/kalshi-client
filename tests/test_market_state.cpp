#include "market_state.hpp"
#include "test_framework.hpp"

namespace {
    std::string dollars(const int cents) { return std::format("0.{:02}00", cents); }

    OrderBookSnapshot snapshot(std::vector<std::pair<int, int>> yes, std::vector<std::pair<int, int>> no) {
        OrderBookSnapshot s{.market_ticker = "T", .market_id = "id"};
        for (auto [p, q]: yes) s.yes_dollars_fp.emplace_back(dollars(p), std::format("{}.00", q));
        for (auto [p, q]: no) s.no_dollars_fp.emplace_back(dollars(p), std::format("{}.00", q));
        return s;
    }

    OrderBookDelta delta(const char *side, const int cents, const std::string &qty) {
        return OrderBookDelta{.market_ticker = "T", .market_id = "id", .price_dollars = dollars(cents),
                              .delta_fp = qty, .side = side, .ts = "", .ts_ms = 0};
    }
} // namespace

TEST(book_snapshot_sets_levels_and_top_of_book) {
    MarketState book;
    CHECK(book.apply_snapshot(snapshot({{40, 10}, {45, 3}}, {{50, 7}, {52, 1}})).has_value());
    CHECK_EQ(book.best_yes_bid(), std::optional<Price>{45});
    CHECK_EQ(book.best_yes_ask(), std::optional<Price>{48}); // 100 - best NO bid (52)
    CHECK_EQ(book.yes()[40], Quantity{1000});                 // quantities are hundredths of a contract
}

TEST(book_empty_sides_have_no_top) {
    MarketState book;
    CHECK(!book.best_yes_bid());
    CHECK(!book.best_yes_ask());
    CHECK(book.apply_snapshot(snapshot({{30, 1}}, {})).has_value());
    CHECK_EQ(book.best_yes_bid(), std::optional<Price>{30});
    CHECK(!book.best_yes_ask());
}

TEST(book_delta_improves_and_removes_best) {
    MarketState book;
    CHECK(book.apply_snapshot(snapshot({{40, 10}, {45, 3}}, {})).has_value());
    CHECK(book.apply_delta(delta("yes", 47, "2.00")).has_value());
    CHECK_EQ(book.best_yes_bid(), std::optional<Price>{47});
    // Removing the best level rescans down to the next populated one.
    CHECK(book.apply_delta(delta("yes", 47, "-2.00")).has_value());
    CHECK_EQ(book.best_yes_bid(), std::optional<Price>{45});
    CHECK(book.apply_delta(delta("yes", 45, "-3.00")).has_value());
    CHECK_EQ(book.best_yes_bid(), std::optional<Price>{40});
    CHECK(book.apply_delta(delta("yes", 40, "-10.00")).has_value());
    CHECK(!book.best_yes_bid());
}

TEST(book_partial_reduction_keeps_best) {
    MarketState book;
    CHECK(book.apply_snapshot(snapshot({{45, 3}}, {})).has_value());
    CHECK(book.apply_delta(delta("yes", 45, "-1.00")).has_value());
    CHECK_EQ(book.best_yes_bid(), std::optional<Price>{45});
    CHECK_EQ(book.yes()[45], Quantity{200});
}

TEST(book_negative_level_is_reported_and_clamped) {
    MarketState book;
    CHECK(book.apply_snapshot(snapshot({{45, 1}}, {})).has_value());
    CHECK(!book.apply_delta(delta("yes", 45, "-5.00")).has_value());
    CHECK_EQ(book.yes()[45], Quantity{0});
    CHECK(!book.best_yes_bid());
}

TEST(book_rejects_invalid_input) {
    MarketState book;
    CHECK(!book.apply_delta(delta("maybe", 45, "1.00")));
    CHECK(!book.apply_delta(delta("yes", 0, "1.00")));  // 0c is not a valid price
    CHECK(!book.apply_delta(OrderBookDelta{.price_dollars = "1.00", .delta_fp = "1.00", .side = "yes"}));
    CHECK(!book.apply_delta(OrderBookDelta{.price_dollars = "0.455", .delta_fp = "1.00", .side = "yes"}));
    CHECK(!book.apply_delta(delta("yes", 45, "lots")));
}

TEST(book_bad_snapshot_leaves_previous_book_intact) {
    MarketState book;
    CHECK(book.apply_snapshot(snapshot({{45, 3}}, {{50, 1}})).has_value());
    auto bad = snapshot({{30, 1}}, {});
    bad.yes_dollars_fp.emplace_back("garbage", "1.00");
    CHECK(!book.apply_snapshot(bad));
    CHECK_EQ(book.best_yes_bid(), std::optional<Price>{45});
    CHECK_EQ(book.best_yes_ask(), std::optional<Price>{50});
}
