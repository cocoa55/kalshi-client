#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

// Parses a decimal string such as "0.2900", "12.00" or "-5" into an integer scaled by 10^Scale,
// using integer arithmetic only. Kalshi sends prices and sizes as decimal strings; going through
// double (stod * 100) turns "0.29" into 28.999... and silently loses a cent on truncation.
//
// Digits beyond Scale are accepted only if they are zero ("0.2900" at Scale 2 is fine, "0.295" is not),
// so a sub-cent price is reported as an error rather than rounded.
template <int Scale>
constexpr std::optional<int64_t> parse_fixed(std::string_view s) {
    static_assert(Scale >= 0 && Scale <= 9);
    if (s.empty())
        return std::nullopt;

    bool negative = false;
    if (s.front() == '-') {
        negative = true;
        s.remove_prefix(1);
    }

    int64_t value = 0;
    int digits = 0;      // total digits consumed, to reject "", "-", "."
    int frac_digits = 0; // digits seen after the decimal point
    bool seen_point = false;

    for (const char c: s) {
        if (c == '.') {
            if (seen_point)
                return std::nullopt;
            seen_point = true;
            continue;
        }
        if (c < '0' || c > '9')
            return std::nullopt;
        ++digits;
        if (seen_point && frac_digits == Scale) {
            if (c != '0') // more precision than we can represent
                return std::nullopt;
            continue;
        }
        const int digit = c - '0';
        if (value > (INT64_MAX - digit) / 10) // value * 10 + digit would overflow
            return std::nullopt;
        value = value * 10 + digit;
        if (seen_point)
            ++frac_digits;
    }
    if (digits == 0)
        return std::nullopt;

    for (; frac_digits < Scale; ++frac_digits) {
        if (value > INT64_MAX / 10)
            return std::nullopt;
        value *= 10;
    }
    return negative ? -value : value;
}

static_assert(parse_fixed<2>("0.29") == 29);
static_assert(parse_fixed<2>("0.2900") == 29);
static_assert(parse_fixed<2>("12.00") == 1200);
static_assert(parse_fixed<2>("-5.00") == -500);
static_assert(parse_fixed<2>("7") == 700);
static_assert(parse_fixed<0>("3.00") == 3);
static_assert(!parse_fixed<2>("0.295"));
static_assert(!parse_fixed<2>("1.2.3"));
static_assert(!parse_fixed<2>("-"));
static_assert(!parse_fixed<2>("abc"));
static_assert(parse_fixed<0>("9223372036854775807") == INT64_MAX);
static_assert(!parse_fixed<0>("9223372036854775808"));
