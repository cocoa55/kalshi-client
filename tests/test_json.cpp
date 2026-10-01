#include "json_lexer.hpp"
#include "json_parser.hpp"
#include "test_framework.hpp"

namespace {
    const JsonObject &obj(const JsonValue &v) { return std::get<JsonObject>(v.data); }
    const std::string &str(const JsonValue &v) { return std::get<std::string>(v.data); }
} // namespace

TEST(json_parses_all_value_kinds) {
    auto v = parse_json(R"( {"s":"x","n":-1.5e3,"t":true,"f":false,"z":null,"a":[1,[]],"o":{}} )");
    CHECK(v.has_value());
    if (!v) return;
    const auto &o = obj(*v);
    CHECK_EQ(str(o.at("s")), std::string{"x"});
    CHECK_EQ(str(o.at("n")), std::string{"-1.5e3"}); // numbers keep their exact text
    CHECK(std::get<bool>(o.at("t").data));
    CHECK(!std::get<bool>(o.at("f").data));
    CHECK(std::holds_alternative<std::monostate>(o.at("z").data));
    CHECK_EQ(std::get<JsonArray>(o.at("a").data).size(), size_t{2});
}

TEST(json_decodes_escapes_and_unicode) {
    auto v = parse_json(R"(["a\"b\\c\/d\n", "\u00e9", "\ud83d\ude00"])");
    CHECK(v.has_value());
    if (!v) return;
    const auto &a = std::get<JsonArray>(v->data);
    CHECK_EQ(str(a[0]), std::string{"a\"b\\c/d\n"});
    CHECK_EQ(str(a[1]), std::string{"\xc3\xa9"});         // U+00E9 as UTF-8
    CHECK_EQ(str(a[2]), std::string{"\xf0\x9f\x98\x80"}); // U+1F600 from a surrogate pair
}

TEST(json_escaped_quote_does_not_end_string) {
    // The previous lexer stopped at the first '"' even when it was escaped.
    auto v = parse_json(R"({"k":"say \"hi\""})");
    CHECK(v.has_value());
    if (v) CHECK_EQ(str(obj(*v).at("k")), std::string{"say \"hi\""});
}

TEST(json_rejects_malformed_input) {
    for (const char *bad: {"", "{", "[", "}", "{\"a\":1", "{\"a\" 1}", "{\"a\":}", "[1,]", "{\"a\":1,}", "[1 2]",
                           "01", "1.", "1.2.3", "-", "1e", "\"abc", "\"bad \\x escape\"", "\"\\u12\"", "tru",
                           "{} extra", "\"\\ud800\"", "\"ctrl \x01 char\"", "{1:2}", "nul"})
        CHECK(!parse_json(std::string_view{bad}));
}

TEST(json_enforces_depth_limit) {
    const std::string ok = std::string(kMaxJsonDepth, '[') + std::string(kMaxJsonDepth, ']');
    const std::string too_deep = std::string(kMaxJsonDepth + 1, '[') + std::string(kMaxJsonDepth + 1, ']');
    CHECK(parse_json(ok).has_value());
    CHECK(!parse_json(too_deep).has_value());
    // Deep enough to overflow the stack without the limit; must fail cleanly instead.
    CHECK(!parse_json(std::string(1'000'000, '[')).has_value());
}

TEST(json_duplicate_keys_keep_first) {
    auto v = parse_json(R"({"k":"first","k":"second"})");
    CHECK(v && str(obj(*v).at("k")) == "first");
}

TEST(json_lexer_tokens_are_views_into_input) {
    const std::string input = R"({"key": 12})";
    JsonLexer lexer{std::string_view{input}};
    CHECK(lexer.next()->type == TokenType::LeftBrace);
    const auto key = lexer.next();
    CHECK(key->type == TokenType::String);
    CHECK(key->text.data() == input.data() + 2); // points into the buffer, no copy
    CHECK(lexer.next()->type == TokenType::Colon);
    CHECK_EQ(lexer.next()->text, std::string_view{"12"});
    CHECK(lexer.next()->type == TokenType::RightBrace);
    CHECK(lexer.next()->type == TokenType::End);
}

TEST(json_bare_negative_number_regression) {
    // Found by fuzzing the previous lexer: any '-' outside a string entered the number branch, but the scan
    // loop only advanced over digits and '.', so it pushed empty tokens forever until memory ran out.
    auto v = parse_json(std::string_view{R"({"position":-3,"pnl":[-0.5,-1e-2]})"});
    CHECK(v.has_value());
    if (v) CHECK_EQ(str(obj(*v).at("position")), std::string{"-3"});
}
