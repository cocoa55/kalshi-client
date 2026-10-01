// Micro-benchmarks for the hot path. Build in Release:
//   cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release
//   ./cmake-build-release/benchmarks            # CPU-only benchmarks
//   ./cmake-build-release/benchmarks --network  # also time TLS handshakes vs keep-alive against the demo API
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <print>
#include <string>
#include <vector>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <unistd.h>

#include "fixed_point.hpp"
#include "http_client.hpp"
#include "json_parser.hpp"
#include "kalshi_auth.hpp"
#include "kalshi_messages.hpp"
#include "market_state.hpp"
#include "net_constants.hpp"

namespace {
template <class T> void do_not_optimize(T const &v) { asm volatile("" : : "r,m"(v) : "memory"); }

// Median of 5 runs, reported per operation.
template <class F> double ns_per_op(const char *name, int iters, F &&f) {
    std::vector<double> runs;
    for (int r = 0; r < 5; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; ++i) f(i);
        const auto t1 = std::chrono::steady_clock::now();
        runs.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / iters);
    }
    std::ranges::sort(runs);
    std::println("{:<44} {:>14.1f} ns/op", name, runs[2]);
    return runs[2];
}

std::string make_snapshot() {
    std::string yes, no;
    for (int p = 1; p <= 40; ++p) yes += std::format(R"({}["0.{:02}00","{}.00"])", p > 1 ? "," : "", p, 100 + p);
    for (int p = 1; p <= 40; ++p) no += std::format(R"({}["0.{:02}00","{}.00"])", p > 1 ? "," : "", p, 200 + p);
    return std::format(R"({{"type":"orderbook_snapshot","sid":2,"seq":1,"msg":{{"market_ticker":"KXMLBGAME-26SEP302000BOSNYY-NYY","market_id":"9b0f6f7e-1c2d-4e5f-8a9b-0c1d2e3f4a5b","yes_dollars_fp":[{}],"no_dollars_fp":[{}]}}}})", yes, no);
}
const std::string kDelta = R"({"type":"orderbook_delta","sid":2,"seq":5,"msg":{"market_ticker":"KXMLBGAME-26SEP302000BOSNYY-NYY","market_id":"9b0f6f7e-1c2d-4e5f-8a9b-0c1d2e3f4a5b","price_dollars":"0.5900","delta_fp":"-12.00","side":"yes","ts":"2026-09-30T23:45:26.123Z","ts_ms":"1790819126123"}})";

std::string write_temp_key() {
    EVP_PKEY *key = EVP_RSA_gen(2048);
    char path[] = "/tmp/kalshi-bench-keyXXXXXX";
    const int fd = mkstemp(path);
    FILE *f = fdopen(fd, "w");
    PEM_write_PrivateKey(f, key, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(f);
    EVP_PKEY_free(key);
    return path;
}
} // namespace

int main(int argc, char **argv) {
    const std::string snapshot = make_snapshot();
    std::println("snapshot message: {} bytes, delta message: {} bytes\n", snapshot.size(), kDelta.size());

    auto as_bytes = [](const std::string &s) { return std::as_bytes(std::span(s)); };
    ns_per_op("decode delta: JSON tree + typed (old path)", 200'000, [&](int) {
        auto m = parse_kalshi_message(*parse_json(kDelta));
        do_not_optimize(m);
    });
    ns_per_op("decode delta: direct from tokens (new)", 200'000, [&](int) {
        auto m = decode_kalshi_message(as_bytes(kDelta));
        do_not_optimize(m);
    });
    ns_per_op("decode snapshot: JSON tree + typed (old path)", 20'000, [&](int) {
        auto m = parse_kalshi_message(*parse_json(snapshot));
        do_not_optimize(m);
    });
    ns_per_op("decode snapshot: direct from tokens (new)", 20'000, [&](int) {
        auto m = decode_kalshi_message(as_bytes(snapshot));
        do_not_optimize(m);
    });

    // Book: apply a delta then read top of book (what the strategy does per update).
    {
        auto snap = parse_kalshi_message(*parse_json(snapshot));
        MarketState book;
        (void) book.apply_snapshot(std::get<OrderBookSnapshot>(snap->msg));
        std::vector<OrderBookDelta> deltas;
        for (int i = 0; i < 1024; ++i) {
            const int p = 1 + (i * 37) % 40;
            deltas.push_back(OrderBookDelta{"T", "id", std::format("0.{:02}00", p), i % 2 ? "-5.00" : "5.00",
                                            i % 3 ? "yes" : "no", "", 0});
        }
        ns_per_op("book apply_delta + best bid/ask", 1'000'000, [&](int i) {
            (void) book.apply_delta(deltas[i & 1023]);
            auto b = book.best_yes_bid();
            auto a = book.best_yes_ask();
            do_not_optimize(b);
            do_not_optimize(a);
        });
    }

    {
        std::vector<std::string> prices;
        for (int p = 1; p < 100; ++p) prices.push_back(std::format("0.{:02}00", p));
        ns_per_op("price: stod * 100 (old)", 2'000'000, [&](int i) {
            auto v = static_cast<int64_t>(std::stod(prices[i % 99]) * 100);
            do_not_optimize(v);
        });
        ns_per_op("price: parse_fixed (new)", 2'000'000, [&](int i) {
            auto v = parse_fixed<kPriceDecimals>(prices[i % 99]);
            do_not_optimize(v);
        });
    }

    const std::string key_path = write_temp_key();
    auto signer = kalshi_auth::Signer::from_credentials({.key_id = "bench", .private_key_path = key_path});
    ns_per_op("sign request (key loaded once)", 500, [&](int i) {
        auto s = signer->auth_headers("POST", std::format("/trade-api/v2/portfolio/orders/{}", i));
        do_not_optimize(s);
    });
    unlink(key_path.c_str());

    if (argc > 1 && std::string_view{argv[1]} == "--network") {
        // Public (unauthenticated) market-data endpoint, so no credentials are needed.
        const std::string path = "/trade-api/v2/markets/KXMLBGAME-26SEP302000BOSNYY-NYY";
        ns_per_op("GET over a new connection (TCP+TLS each time)", 5, [&](int) {
            HttpClient fresh{std::string{kKalshiHost}};
            auto r = fresh.request("GET", path);
            do_not_optimize(r);
        });
        HttpClient http{std::string{kKalshiHost}};
        (void) http.connect();
        ns_per_op("GET over a kept-alive connection", 5, [&](int) {
            auto r = http.request("GET", path);
            do_not_optimize(r);
        });
        std::println("(kept-alive client opened {} connection(s) for 25 requests)", http.connections_opened());
    }
}
