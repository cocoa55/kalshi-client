# kalshi-client

A C++23 client for Kalshi's trading API, built from scratch (raw POSIX sockets, OpenSSL for TLS, no networking libraries) as a learning project.

## What it does at the moment
Connects to Kalshi's WebSocket API, performs a TLS handshake, upgrades to WebSocket, parses incoming frames, and responds to server pings with pongs. Built from scratch using raw POSIX sockets and OpenSSL with no external networking or parsing libraries.
Parses incoming JSON market data into C++ structs using a custom lexer and parser. Tracks market state, manages orders, and updates positions on fills.
Runs an autonomous mean-reversion strategy that trades a single market continuously, reconnecting on its own when the connection drops.

## Status

- [x] Phase 1: TLS connection and WebSocket handshake
- [x] Phase 2: WebSocket frame parser and builder
- [x] Phase 3: JSON parser
- [x] Phase 4: REST order management
- [x] Phase 5: Order management system
- [x] Phase 6: Trading logic

## Trading strategy

`MeanReversionStrategy` (`src/trading/strategy.cpp`) trades one market:

1. **Fair value** comes from a time-decayed EMA of the YES mid price (τ = 60s). The weight depends on elapsed time, not message count, so a burst of deltas doesn't skew it.
2. **Entry:** buy when the best YES ask is ≥ 3¢ below fair value. Sell when the best YES bid is ≥ 3¢ above it.
3. **Filters:** no trading during a 60s warmup, on one-sided or crossed books, or when the spread is wider than 4¢.
4. **Risk:** a hard cap of ±5 contracts, 1 contract per order, and a 5s cooldown between orders.
5. **Orders are immediate-or-cancel.** Nothing rests on the book, so no cancel logic is needed and a disconnect can't leave stale orders behind.

Position comes from the WebSocket `fill` channel. Fills reported by the REST response but not yet confirmed on the WebSocket still count toward the position limit. This closes the race where a second order could go out before the first fill arrives.

The bot runs until `SIGINT` or `SIGTERM`. When the connection drops, it reconnects with exponential backoff (1s up to 60s) and rebuilds the book from a fresh snapshot. A 30s socket receive timeout catches connections that die silently.

Tunable parameters are in `StrategyConfig` (`include/strategy.hpp`).

**Exchange shards:** Kalshi keeps a separate balance for each matching-engine shard (0 = default, 2 = crypto/commodities, 3 = tennis/baseball/basketball). Orders on a shard with no money fail with `insufficient_shard_balance`. To move demo funds from shard 0, run `./cmake-build-debug/kalshi-client --fund-shard <index> <dollars>`.

**Known limitations:** the bot starts out assuming a flat position (it doesn't fetch existing positions over REST), it doesn't detect sequence gaps, and it trades a single market. The bot pays the spread on every entry, so this strategy demonstrates the system end to end. It isn't expected to be profitable.

## Prerequisites

- A C++23 compiler (GCC 14+ / Clang 17+)
- CMake 3.20+
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
├── main.cpp
├── include/
│   ├── tcp_socket.hpp
│   ├── tls_socket.hpp
│   ├── web_socket.hpp
│   ├── web_socket_frame.hpp
│   ├── frame_parser.hpp
│   ├── frame_builder.hpp
│   ├── kalshi_auth.hpp
│   ├── json_parser.hpp
│   ├── json_lexer.hpp
│   ├── openssl_util.hpp
│   ├── kalshi_messages.hpp
│   ├── net_constants.hpp
│   ├── parse_error.hpp
│   ├── http_client.hpp
│   ├── time_util.hpp
│   ├── market_state.hpp
│   ├── order_tracker.hpp
│   ├── position_tracker.hpp
│   └── strategy.hpp
├── src/
│   ├── encoding/
│   │   ├── json_lexer.cpp
│   │   └── json_parser.cpp
│   ├── network/
│   │   ├── tcp_socket.cpp
│   │   ├── tls_socket.cpp
│   │   └── web_socket.cpp
│   ├── protocol/
│   │   ├── frame_parser.cpp
│   │   ├── frame_builder.cpp
│   │   ├── http_client.cpp
│   │   ├── kalshi_messages.cpp
│   │   └── kalshi_auth.cpp
│   └── trading/
│       ├── market_state.cpp
│       └── strategy.cpp
└── tests/
```
