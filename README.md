# kalshi-client

A C++23 trading client for Kalshi's API, built from scratch as a low-level systems project: raw POSIX sockets, OpenSSL for TLS, and hand-written WebSocket, HTTP/1.1 and JSON implementations. It uses no networking or parsing libraries.

It streams a market's order book over a WebSocket, keeps a local copy of the book, and sends orders over a persistent HTTPS connection. A deliberately simple strategy drives it end to end.

## What's inside

| Layer | What it does |
|---|---|
| **TCP** (`tcp_socket`) | `getaddrinfo` + non-blocking `connect` with a 5s deadline, `TCP_NODELAY`, 30s receive timeout, RAII fd ownership |
| **TLS** (`tls_socket`) | OpenSSL with certificate + hostname verification and SNI |
| **WebSocket** (`web_socket`, `frame_parser`, `frame_builder`) | RFC 6455 upgrade handshake, framing with all three length encodings, client masking, ping/pong; rejects reserved bits, oversized control frames and payloads over 16 MiB |
| **HTTP/1.1** (`http_client`, `http_response`) | Keep-alive connection reuse with dead-connection detection, `Content-Length` and chunked bodies, safe retry rules for orders |
| **JSON** (`json_lexer`, `json_parser`, `kalshi_decoder`) | Pull lexer producing `string_view` tokens, strict RFC 8259 grammar with escapes and UTF-16 surrogates, depth limit; WebSocket messages decode straight from tokens into typed structs without an intermediate tree |
| **Auth** (`kalshi_auth`) | RSA-PSS/SHA-256 request signing with the key parsed once at startup |
| **Order book** (`market_state`) | Fixed `std::array` per side indexed by price in cents, incrementally maintained best bid, integer fixed-point parsing |
| **Bot** (`trading_bot`) | Single-threaded loop with sequence-gap detection, exponential-backoff reconnect, and per-stage latency percentiles |

## Performance

Measured with `bench/benchmarks.cpp` (Release, median of 5 runs). "Before" is the Phase 6 code.

| Operation | Before | After | |
|---|---:|---:|---|
| Decode an order-book delta (265 B) | 2.29 µs | 0.73 µs | 3.1× |
| Decode a 40-level snapshot (1.8 KB) | 23.2 µs | 6.8 µs | 3.4× |
| Apply a delta + read top of book | 196 ns | 97 ns | 2.0× |
| Parse a price string | 80 ns | 12 ns | 6.7× |
| Sign a request | 903 µs | 531 µs | 1.7× |
| HTTPS request to the demo API | 249 ms | 82 ms | saves one TCP + TLS handshake (~168 ms) per order |

The bot also prints its own latency percentiles on shutdown: decode, book + strategy, tick-to-order-sent, and order round trip.

## Testing

- **Unit tests** (`tests/`): 45 tests across every layer, using a small self-registering test framework with no dependencies.
- **Sanitizers:** the whole suite runs clean under AddressSanitizer + UndefinedBehaviorSanitizer.
- **Fuzzing** (`fuzz/`): four libFuzzer targets, each checking a property, not just "doesn't crash":
  - `fuzz_json`: print → parse → print round-trips unchanged
  - `fuzz_decoder_differential`: the direct decoder accepts exactly what the JSON-tree path accepts and produces identical messages
  - `fuzz_frame_parser`: never over-reads; rebuild → reparse preserves the payload
  - `fuzz_http_response`: consistent under incremental delivery (what the client relies on when reading in chunks)

Pointed at the previous JSON lexer, the fuzzer found within seconds that any bare negative number (`{"position":-3}`) sent it into an infinite loop that consumed all memory.

```sh
# Unit tests
cmake -S . -B cmake-build-debug && cmake --build cmake-build-debug && ./cmake-build-debug/unit_tests

# Unit tests under ASan + UBSan
CXX=clang++ cmake -S . -B cmake-build-asan -DKALSHI_SANITIZE=ON && cmake --build cmake-build-asan && ./cmake-build-asan/unit_tests

# Fuzzers (Clang). New inputs go to the first directory (fuzz/work/, git-ignored);
# the committed seeds in fuzz/corpus/ are only read.
CXX=clang++ cmake -S . -B cmake-build-fuzz -DKALSHI_FUZZ=ON && cmake --build cmake-build-fuzz
mkdir -p fuzz/work/json && ./cmake-build-fuzz/fuzz_json -max_total_time=60 -artifact_prefix=fuzz/work/ fuzz/work/json fuzz/corpus/json

# Benchmarks (add --network to time real handshakes against the demo API)
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release
./cmake-build-release/benchmarks --network
```

## Status

- [x] Phase 1: TLS connection and WebSocket handshake
- [x] Phase 2: WebSocket frame parser and builder
- [x] Phase 3: JSON parser
- [x] Phase 4: REST order management
- [x] Phase 5: Order management system
- [x] Phase 6: Trading logic
- [x] Phase 7: Performance, testing and hardening

## The strategy

The strategy exists to exercise the system, not to make money. It compares the best bid and ask against a time-decayed moving average of the mid price. When the market moves 3¢ or more away from that average, it sends a one-contract immediate-or-cancel order, with a ±5 contract position cap and a 5s cooldown. Immediate-or-cancel orders never rest on the book, so there's nothing to cancel if the bot disconnects. Parameters are in `StrategyConfig` (`include/strategy.hpp`).

**Exchange shards:** Kalshi keeps a separate balance for each matching-engine shard (0 = default, 2 = crypto/commodities, 3 = tennis/baseball/basketball). Orders on a shard with no money fail with `insufficient_shard_balance`. To move demo funds from shard 0, run `./cmake-build-debug/kalshi-client --fund-shard <index> <dollars>`.

**Known limitations:** single market only; the bot assumes a flat position at startup; blocking I/O on one thread (an order blocks market-data processing for its round trip); fragmented WebSocket messages aren't reassembled (Kalshi doesn't send them).

## Prerequisites

- A C++23 compiler (GCC 14+ / Clang 17+; Clang for fuzzing)
- CMake 4.0+
- OpenSSL development headers (`libssl-dev` / `openssl-devel`)

## Building

```sh
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug
```

## Kalshi demo credentials

The client authenticates every request with an RSA key pair from a Kalshi **demo** account (mock funds only, safe to use fake signup info).

1. Sign up at `demo.kalshi.co/sign-up` — note the `.co`, not `.com`. Real personal info isn't required.
2. Log in → Profile Settings → API Keys → **Create New API Key**. This shows a **Key ID** and an RSA **private key** exactly once — save the private key to a file immediately; Kalshi doesn't store it.
3. **Never commit the private key file.** Keep it outside the repo (e.g. `~/.kalshi/demo_private_key.pem`) — `.gitignore` also blocks `*.pem`/`*.key` as a safety net.

The client reads two environment variables:

| Variable | Value |
|---|---|
| `KALSHI_API_KEY_ID` | the Key ID from step 2 |
| `KALSHI_PRIVATE_KEY_PATH` | absolute path to the saved private key PEM file |

### Running from a terminal

```fish
# fish
set -x KALSHI_API_KEY_ID "your-key-id"
set -x KALSHI_PRIVATE_KEY_PATH "/path/to/demo_private_key.pem"
./cmake-build-debug/kalshi-client [MARKET_TICKER]
```

```bash
# bash / zsh
export KALSHI_API_KEY_ID="your-key-id"
export KALSHI_PRIVATE_KEY_PATH="/path/to/demo_private_key.pem"
./cmake-build-debug/kalshi-client [MARKET_TICKER]
```

### Running from CLion

**Run → Edit Configurations… → `kalshi-client` → Environment variables →** add `KALSHI_API_KEY_ID` and `KALSHI_PRIVATE_KEY_PATH`.

## Project structure

```
kalshi-client/
├── CMakeLists.txt
├── main.cpp                  # entry point, signal handling, --fund-shard
├── include/                  # headers (one per component below, plus fixed_point, latency_stats, ...)
├── src/
│   ├── encoding/             # json_lexer, json_parser
│   ├── network/              # tcp_socket, tls_socket, web_socket
│   ├── protocol/             # frame_parser/builder, http_client, http_response, kalshi_auth,
│   │                         # kalshi_messages, kalshi_decoder
│   └── trading/              # market_state, strategy, trading_bot
├── tests/                    # unit tests + minimal test framework
├── fuzz/                     # libFuzzer targets and seed corpora
└── bench/                    # micro-benchmarks
```
