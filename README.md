# kalshi-client

A C++23 client for Kalshi's trading API, built from scratch (raw POSIX sockets, OpenSSL for TLS, no networking libraries) as a learning project.

## What it does at the moment

Connects to Kalshi's WebSocket API, performs a TLS handshake, upgrades to WebSocket, parses incoming frames, and responds to server pings with pongs. Built from scratch using raw POSIX sockets and OpenSSL with no external networking or parsing libraries.

## Status

- [x] Phase 1: TLS connection and WebSocket handshake
- [x] Phase 2: WebSocket frame parser and builder
- [ ] Phase 3: JSON parser
- [ ] Phase 4: REST order management
- [ ] Phase 5: Order management system
- [ ] Phase 6: Trading logic

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
./cmake-build-debug/kalshi-client
```

```bash
# bash / zsh
export KALSHI_API_KEY_ID="your-key-id"
export KALSHI_PRIVATE_KEY_PATH="/path/to/demo_private_key.pem"
./cmake-build-debug/kalshi-client
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
│   └── kalshi_auth.hpp
├── src/
│   ├── network/
│   │   ├── tcp_socket.cpp
│   │   ├── tls_socket.cpp
│   │   └── web_socket.cpp
│   └── protocol/
│       ├── frame_parser.cpp
│       ├── frame_builder.cpp
│       └── kalshi_auth.cpp
└── tests/
```
