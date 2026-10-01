// Tree-free decoding of Kalshi WebSocket messages.
//
// parse_json + parse_kalshi_message builds a generic JSON tree (a flat_map per object, a vector per array,
// a std::string per scalar) and then copies fields out of it. Measured on a 40-level snapshot, building that
// tree was ~13 of the ~24 us total. This decoder walks the token stream once and records string_views into
// the payload for just the fields Kalshi messages use, then builds the typed struct directly.
//
// It must accept and reject exactly what the tree-based path does (fuzz/fuzz_decoder_differential.cpp checks
// this), so it mirrors that path's rules:
//   * the whole payload is still validated as JSON, including values that are skipped;
//   * the first occurrence of a duplicate key wins (flat_map::emplace), even if its value has the wrong type;
//   * numbers are accepted wherever a string is expected (the tree stores numbers as their text);
//   * nesting depth is counted the same way, against kMaxJsonDepth.
// Strings containing escape sequences would need decoding before comparison, so if any appear the decoder
// bails out to the tree-based path. Kalshi's market data never contains them.
#include <charconv>
#include <optional>
#include <utility>

#include "json_lexer.hpp"
#include "json_parser.hpp"
#include "kalshi_messages.hpp"

namespace {
    using LevelViews = std::vector<std::pair<std::string_view, std::string_view>>;

    struct Scalar {
        bool seen{false};
        std::optional<std::string_view> value; // set only if the first occurrence was a string or number
    };
    struct LevelsField {
        bool seen{false};
        bool present{false}; // first occurrence was an array
        bool valid{true};    // every element was a 2-element array of strings
        LevelViews levels;
    };
    struct Fields {
        Scalar type, sid, seq;
        bool msg_seen{false}, msg_is_object{false};
        Scalar market_ticker, market_id, price_dollars, delta_fp, side, ts, ts_ms, order_id, count_fp, action;
        LevelsField yes, no;
    };

    using Status = std::expected<void, JsonError>;

    std::optional<uint64_t> parse_u64(const std::string_view s) {
        uint64_t value{};
        const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec != std::errc{} || end != s.data() + s.size())
            return std::nullopt;
        return value;
    }

    constexpr bool is_text(const TokenType t) { return t == TokenType::String || t == TokenType::Number; }

    class Decoder {
        JsonLexer _lexer;

        std::unexpected<JsonError> fail(const char *what) const {
            return std::unexpected(JsonError{what, _lexer.position()});
        }

    public:
        bool needs_fallback{false};

        explicit Decoder(std::span<const std::byte> bytes) : _lexer(bytes) {}

        std::expected<Token, JsonError> next() {
            auto token = _lexer.next();
            if (token && token->type == TokenType::String && token->has_escapes) {
                needs_fallback = true;
                return std::unexpected(JsonError{"escaped string; falling back", _lexer.position()});
            }
            return token;
        }

        // Called after '{' has been consumed. f(key, first_value_token, depth) must consume the value.
        template <class F>
        Status for_each_member(const int depth, F &&f) {
            auto token = next();
            if (!token) return std::unexpected(token.error());
            if (token->type == TokenType::RightBrace)
                return {};
            while (true) {
                if (token->type != TokenType::String)
                    return fail("Expected string key in object");
                const std::string_view key = token->text;
                auto colon = next();
                if (!colon) return std::unexpected(colon.error());
                if (colon->type != TokenType::Colon)
                    return fail("Expected ':' after object key");
                auto value = next();
                if (!value) return std::unexpected(value.error());
                if (auto r = f(key, *value, depth); !r)
                    return r;
                auto sep = next();
                if (!sep) return std::unexpected(sep.error());
                if (sep->type == TokenType::RightBrace)
                    return {};
                if (sep->type != TokenType::Comma)
                    return fail("Expected ',' or '}' after object value");
                token = next();
                if (!token) return std::unexpected(token.error());
            }
        }

        // Called after '[' has been consumed. f(first_value_token, depth) must consume the element.
        template <class F>
        Status for_each_element(const int depth, F &&f) {
            auto token = next();
            if (!token) return std::unexpected(token.error());
            if (token->type == TokenType::RightBracket)
                return {};
            while (true) {
                if (auto r = f(*token, depth); !r)
                    return r;
                auto sep = next();
                if (!sep) return std::unexpected(sep.error());
                if (sep->type == TokenType::RightBracket)
                    return {};
                if (sep->type != TokenType::Comma)
                    return fail("Expected ',' or ']' after array value");
                token = next();
                if (!token) return std::unexpected(token.error());
            }
        }

        // Validates and discards one value, given its first token.
        Status skip_value(const Token &first, const int depth) {
            switch (first.type) {
                case TokenType::String:
                case TokenType::Number:
                case TokenType::True:
                case TokenType::False:
                case TokenType::Null:
                    return {};
                case TokenType::LeftBrace:
                case TokenType::LeftBracket:
                    break;
                case TokenType::End:
                    return fail("Unexpected end of JSON");
                default:
                    return fail("Unexpected token in value position");
            }
            if (depth >= kMaxJsonDepth)
                return fail("JSON nested too deeply");
            if (first.type == TokenType::LeftBrace)
                return for_each_member(depth + 1, [&](std::string_view, const Token &v, const int d) { return skip_value(v, d); });
            return for_each_element(depth + 1, [&](const Token &v, const int d) { return skip_value(v, d); });
        }

        Status scalar(Scalar &field, const Token &value, const int depth) {
            if (!field.seen) {
                field.seen = true;
                if (is_text(value.type)) {
                    field.value = value.text;
                    return {};
                }
            }
            return skip_value(value, depth);
        }

        Status levels(LevelsField &field, const Token &value, const int depth) {
            if (field.seen || value.type != TokenType::LeftBracket) {
                field.seen = true;
                return skip_value(value, depth);
            }
            field.seen = field.present = true;
            if (depth >= kMaxJsonDepth)
                return fail("JSON nested too deeply");
            return for_each_element(depth + 1, [&](const Token &element, const int d) -> Status {
                if (element.type != TokenType::LeftBracket) {
                    field.valid = false;
                    return skip_value(element, d);
                }
                if (d >= kMaxJsonDepth)
                    return fail("JSON nested too deeply");
                std::string_view pair[2];
                int count = 0;
                bool all_text = true;
                auto r = for_each_element(d + 1, [&](const Token &x, const int dd) -> Status {
                    if (is_text(x.type)) {
                        if (count < 2)
                            pair[count] = x.text;
                        ++count;
                        return {};
                    }
                    all_text = false;
                    ++count;
                    return skip_value(x, dd);
                });
                if (!r)
                    return r;
                if (count == 2 && all_text)
                    field.levels.emplace_back(pair[0], pair[1]);
                else
                    field.valid = false;
                return {};
            });
        }

        Status msg_member(Fields &f, const std::string_view key, const Token &value, const int depth) {
            if (key == "market_ticker") return scalar(f.market_ticker, value, depth);
            if (key == "market_id") return scalar(f.market_id, value, depth);
            if (key == "price_dollars") return scalar(f.price_dollars, value, depth);
            if (key == "delta_fp") return scalar(f.delta_fp, value, depth);
            if (key == "side") return scalar(f.side, value, depth);
            if (key == "ts") return scalar(f.ts, value, depth);
            if (key == "ts_ms") return scalar(f.ts_ms, value, depth);
            if (key == "order_id") return scalar(f.order_id, value, depth);
            if (key == "count_fp") return scalar(f.count_fp, value, depth);
            if (key == "action") return scalar(f.action, value, depth);
            if (key == "yes_dollars_fp") return levels(f.yes, value, depth);
            if (key == "no_dollars_fp") return levels(f.no, value, depth);
            return skip_value(value, depth);
        }

        Status root_member(Fields &f, const std::string_view key, const Token &value, const int depth) {
            if (key == "type") return scalar(f.type, value, depth);
            if (key == "sid") return scalar(f.sid, value, depth);
            if (key == "seq") return scalar(f.seq, value, depth);
            if (key == "msg" && !f.msg_seen) {
                f.msg_seen = true;
                if (value.type == TokenType::LeftBrace) {
                    f.msg_is_object = true;
                    if (depth >= kMaxJsonDepth)
                        return fail("JSON nested too deeply");
                    return for_each_member(depth + 1, [&](const std::string_view k, const Token &v, const int d) {
                        return msg_member(f, k, v, d);
                    });
                }
            }
            return skip_value(value, depth);
        }

        // Fills `f` from the payload. A non-object root is an error, as in parse_kalshi_message.
        Status decode(Fields &f) {
            auto first = next();
            if (!first) return std::unexpected(first.error());
            if (first->type != TokenType::LeftBrace) {
                if (auto r = skip_value(*first, 0); !r)
                    return r;
                auto end = next();
                if (!end) return std::unexpected(end.error());
                if (end->type != TokenType::End)
                    return fail("Trailing characters after JSON value");
                return fail("Root JSON is not an object");
            }
            if (auto r = for_each_member(1, [&](const std::string_view k, const Token &v, const int d) {
                    return root_member(f, k, v, d);
                }); !r)
                return r;
            auto end = next();
            if (!end) return std::unexpected(end.error());
            if (end->type != TokenType::End)
                return fail("Trailing characters after JSON value");
            return {};
        }
    };

    std::string str(const std::string_view v) { return std::string{v}; }

    // A plain reserve + emplace_back loop rather than views::transform | ranges::to: this is on the snapshot
    // hot path, and constructing each level in place measured ~10% faster than ranges::to's construct-then-move.
    std::vector<PriceLevel> to_levels(const LevelViews &views) {
        std::vector<PriceLevel> out;
        out.reserve(views.size());
        for (const auto &[price, qty]: views)
            out.emplace_back(price, qty);
        return out;
    }

    std::expected<Message, std::string> build(const Fields &f) {
        if (!f.type.value)
            return std::unexpected("Missing or invalid 'type' field");
        const std::string_view type = *f.type.value;
        const auto sid = static_cast<uint32_t>(f.sid.value.and_then(parse_u64).value_or(0));
        const auto seq = static_cast<uint32_t>(f.seq.value.and_then(parse_u64).value_or(0));
        auto make = [&](auto &&payload) {
            return Message{.type = str(type), .sid = sid, .seq = seq, .msg = std::forward<decltype(payload)>(payload)};
        };

        if (type == "subscribed")
            return make(std::monostate{});
        if (!f.msg_is_object)
            return std::unexpected("Missing 'msg' object");

        if (type == "orderbook_delta") {
            const auto ts_ms = f.ts_ms.value.and_then(parse_u64);
            if (!f.market_ticker.value || !f.market_id.value || !f.price_dollars.value || !f.delta_fp.value ||
                !f.side.value || !f.ts.value || !ts_ms)
                return std::unexpected("Invalid Key");
            return make(OrderBookDelta{.market_ticker = str(*f.market_ticker.value),
                                       .market_id = str(*f.market_id.value),
                                       .price_dollars = str(*f.price_dollars.value),
                                       .delta_fp = str(*f.delta_fp.value),
                                       .side = str(*f.side.value),
                                       .ts = str(*f.ts.value),
                                       .ts_ms = *ts_ms});
        }
        if (type == "orderbook_snapshot") {
            if (!f.market_ticker.value || !f.market_id.value)
                return std::unexpected("Missing required fields in snapshot");
            if ((f.yes.present && !f.yes.valid) || (f.no.present && !f.no.valid))
                return std::unexpected("Malformed price level");
            return make(OrderBookSnapshot{.market_ticker = str(*f.market_ticker.value),
                                          .market_id = str(*f.market_id.value),
                                          .yes_dollars_fp = f.yes.present ? to_levels(f.yes.levels) : std::vector<PriceLevel>{},
                                          .no_dollars_fp = f.no.present ? to_levels(f.no.levels) : std::vector<PriceLevel>{}});
        }
        if (type == "fill") {
            if (!f.action.value)
                return std::unexpected("Missing action field");
            FillAction action;
            if (*f.action.value == "buy") action = FillAction::Buy;
            else if (*f.action.value == "sell") action = FillAction::Sell;
            else return std::unexpected("Unknown action: " + str(*f.action.value));
            if (!f.order_id.value || !f.market_ticker.value || !f.count_fp.value)
                return std::unexpected("Missing fields in fill");
            return make(Fill{.order_id = str(*f.order_id.value),
                             .market_ticker = str(*f.market_ticker.value),
                             .count_fp = str(*f.count_fp.value),
                             .action = action});
        }
        return std::unexpected("Unknown message type: " + str(type));
    }
} // namespace

std::expected<Message, std::string> decode_kalshi_message(std::span<const std::byte> bytes) {
    Decoder decoder{bytes};
    Fields fields;
    if (auto r = decoder.decode(fields); !r) {
        if (!decoder.needs_fallback)
            return std::unexpected(r.error().message());
        return parse_json(bytes).and_then(parse_kalshi_message);
    }
    return build(fields);
}
