#include "fixed_point.hpp"
#include "test_framework.hpp"

TEST(fixed_point_parses_kalshi_prices_exactly) {
    // Every whole-cent price must round-trip; this is exactly where stod * 100 truncated 0.29 to 28.
    for (int cents = 1; cents < 100; ++cents) {
        CHECK_EQ(parse_fixed<2>(std::format("0.{:02}", cents)), std::optional<int64_t>{cents});
        CHECK_EQ(parse_fixed<2>(std::format("0.{:02}00", cents)), std::optional<int64_t>{cents});
    }
}

TEST(fixed_point_handles_signs_and_scales) {
    CHECK_EQ(parse_fixed<2>("-12.50"), std::optional<int64_t>{-1250});
    CHECK_EQ(parse_fixed<0>("5"), std::optional<int64_t>{5});
    CHECK_EQ(parse_fixed<0>("5.000"), std::optional<int64_t>{5});
    CHECK_EQ(parse_fixed<4>("0.5"), std::optional<int64_t>{5000});
    CHECK_EQ(parse_fixed<2>(".5"), std::optional<int64_t>{50});
}

TEST(fixed_point_rejects_malformed_and_lossy_input) {
    for (const char *bad: {"", "-", ".", "1.2.3", "abc", "1e5", "+1", "0.295", "1.001", " 1", "1 "})
        CHECK(!parse_fixed<2>(bad));
    CHECK(!parse_fixed<0>("0.50")); // fractional contracts are an error, not silently truncated
}

TEST(fixed_point_rejects_overflow) {
    CHECK(parse_fixed<0>("9223372036854775800").has_value());
    CHECK(!parse_fixed<0>("99999999999999999999"));
    CHECK(!parse_fixed<2>("999999999999999999.00")); // fits as digits, overflows once scaled
}
