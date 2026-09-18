# TLS + Kalshi Auth on the WebSocket Handshake

A walkthrough of what changed in `TcpSocket` -> `TlsSocket` -> `WebSocket` -> `kalshi_auth`, written up the way I'd explain it if you were reading this diff cold.

## Where we started

The client was doing a plaintext HTTP GET with `Upgrade: websocket` over a raw `TcpSocket`, on port 80, against the wrong path (`/trade-api/ws/trade/v2`), with a hardcoded `Sec-WebSocket-Key`. None of that will ever get past Kalshi's edge — they only serve WSS on 443, the path was wrong, and a fixed key means the connection would technically violate RFC 6455 even if the server let it through.

## 1. TcpSocket stays dumb, on purpose

`TcpSocket` (`src/network/tcp_socket.cpp`) does exactly one job: resolve a host/port and hand back a connected fd via `std::expected<int, std::string>`. We didn't teach it about TLS. Instead there's a new `TlsSocket` that *owns* a `TcpSocket` and layers encryption on top of its fd. Keep the layers separate — if we ever need a plaintext connection (local testing, a proxy, whatever), `TcpSocket` is still there unmodified.

One real bug fix here: `send_data` used to take `std::string&` (non-const, by reference). That forces every caller to have a named mutable lvalue lying around for no reason — you can't pass a temporary or a `const std::string`. Changed it to `const std::string&`, which is the correct signature for "read this data and send it, don't touch it."

## 2. TlsSocket — the actual OpenSSL wiring

File: `include/tls_socket.hpp` / `src/network/tls_socket.cpp`.

The connection sequence, if you haven't done raw OpenSSL before:

1. `SSL_CTX_new(TLS_client_method())` — a context object that holds protocol settings, trust store, etc. You make one of these and can reuse it for many connections; we make one per `TlsSocket` since we only ever open one connection.
2. `SSL_CTX_set_default_verify_paths()` + `SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr)` — **this is the part that's easy to skip and dangerous to skip.** Without it, OpenSSL will happily complete a TLS handshake with a self-signed or wrong-hostname cert, and you've built a client that's trivially MITM'able. We point it at the system trust store and tell it to actually verify.
3. `_tcp_socket.connect(...)` gets us a raw fd.
4. `SSL_new(ctx)` — a new `SSL*` per connection, bound to the context's settings.
5. `SSL_set_fd(ssl, fd)` — wire the TLS state machine to the socket.
6. `SSL_set_tlsext_host_name(...)` (SNI) and `SSL_set1_host(...)` (hostname verification target) — **both are required.** SNI tells the server which cert to present (most hosts are behind shared load balancers / CDNs and won't know which cert you want otherwise); `SSL_set1_host` tells *OpenSSL* what hostname to check the returned cert against. Set one without the other and you either fail to connect or silently stop checking the hostname.
7. `SSL_connect(ssl)` — does the actual handshake. Non-1 return means failure; we pull the OpenSSL error queue via `ERR_get_error()` / `ERR_error_string_n()` to get a human-readable reason instead of just "it failed."

### RAII, not manual cleanup

`SSL_CTX*` and `SSL*` are C handles — normally you'd pair `SSL_CTX_new`/`SSL_new` with manual `SSL_CTX_free`/`SSL_free` calls in a destructor, and if you get an early return wrong somewhere, you leak. Instead:

```cpp
std::unique_ptr<SSL_CTX, SslCtxDeleter> _ctx;
std::unique_ptr<SSL, SslDeleter>        _ssl;
```

with small deleter structs calling the right `_free` function. This buys us three things for free: no destructor to write, no possible leak on an early `return std::unexpected(...)`, and `TlsSocket` becomes move-only automatically (you can't accidentally copy an `SSL*` and get a double-free, because `unique_ptr` isn't copyable — the compiler blocks it).

## 3. Randomizing the Sec-WebSocket-Key

`src/network/web_socket.cpp`, `generate_websocket_key()`.

The key in the handshake isn't for security in any real sense — it's there so the server can prove it actually understood the request was a WebSocket upgrade (it echoes back `Sec-WebSocket-Accept` derived from it) and so caches/proxies that don't understand WebSocket don't mangle the exchange. But the spec (RFC 6455 §4.1) requires 16 random bytes, base64-encoded, generated fresh per connection — a hardcoded key is spec-non-compliant and some servers will reject it outright.

```cpp
RAND_bytes(raw.data(), raw.size());       // 16 CSPRNG bytes
EVP_EncodeBlock(encoded.data(), raw.data(), raw.size());  // base64
```

Note it's `RAND_bytes`, not `std::rand()` or `<random>`'s `mt19937`. This doesn't need to be cryptographically unpredictable for security purposes, but we're already linking OpenSSL for TLS, so there's no reason to reach for a weaker generator — `RAND_bytes` is the CSPRNG OpenSSL already has warmed up.

## 4. Kalshi request signing (`kalshi_auth`)

New files: `include/kalshi_auth.hpp`, `src/protocol/kalshi_auth.cpp`.

Kalshi doesn't do session cookies or bearer tokens for this endpoint — every request is signed with your RSA private key. The scheme:

```
message   = timestamp_ms + "GET" + "/trade-api/ws/v2"
signature = base64( RSA-PSS-SHA256_sign(message, your_private_key) )
```

sent as three headers: `KALSHI-ACCESS-KEY` (your key id, not secret), `KALSHI-ACCESS-TIMESTAMP`, `KALSHI-ACCESS-SIGNATURE`.

Implementation notes, since this is the part most people haven't done before:

- We use the **EVP** signing API (`EVP_DigestSignInit` / `Update` / `Final`), not the older algorithm-specific `RSA_sign`. EVP is the modern, algorithm-agnostic OpenSSL interface — same code shape works if Kalshi ever switched to Ed25519, you'd just change the key type.
- `EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING)` — Kalshi specifically wants PSS padding, not plain PKCS#1 v1.5. Get this wrong and every signature verifies as garbage on their end, with no useful error on ours.
- `EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, RSA_PSS_SALTLEN_DIGEST)` — salt length equal to the digest size (32 bytes for SHA-256). This has to match what Kalshi's server expects when it reconstructs and verifies the signature; it's the OpenSSL equivalent of Python `cryptography`'s `padding.PSS.DIGEST_LENGTH`, which is what Kalshi's own example code uses.
- `EVP_DigestSignFinal` gets called **twice** — once with a null buffer to ask "how many bytes will the signature be," then again with a properly sized buffer to actually get it. That's a standard OpenSSL pattern for anything with variable-length output; get used to seeing it.

Credentials come from `KALSHI_API_KEY_ID` and `KALSHI_PRIVATE_KEY_PATH` (a path to your PEM private key) in the environment — never hardcoded, never committed. If either is missing, `WebSocket::connect` fails immediately with a clear message instead of attempting a doomed anonymous handshake.

## 5. Not lying about success

Original code printed "WebSocket connection established" as soon as *any* HTTP response came back — including a 401. Fixed `WebSocket::connect` to check the status line:

```cpp
if (!response.starts_with("HTTP/1.1 101")) {
    return std::unexpected("Handshake rejected (expected HTTP 101 Switching Protocols)");
}
```

Small thing, but "the function returned success" should mean the thing you asked for actually happened, not just "a network round-trip completed."

## 6. Where things stand

Tested end-to-end against `wss://external-api-ws.demo.kalshi.co/trade-api/ws/v2`:

- TLS handshake: works (real cert verification, SNI, no hangs/errors).
- Handshake without credentials: fails fast with `Missing Kalshi credentials: ...` instead of sending a request that would 401 anyway.
- Handshake **with** credentials: not yet tested — needs a real `KALSHI_API_KEY_ID` / `KALSHI_PRIVATE_KEY_PATH` from Kalshi's dashboard, which nobody but the account owner has. That's the next thing to run once you've got a demo API key generated.

## Modern C++ used throughout

If you're reading this to pick up patterns for the rest of the codebase:

- `std::expected<T, std::string>` everywhere instead of exceptions or error codes — every fallible function returns one, callers `.has_value()` check and propagate.
- `std::unique_ptr` with custom deleters for every C-API handle (`addrinfo`, `SSL_CTX`, `SSL`, `EVP_PKEY`, `EVP_MD_CTX`, `BIO`) — never a raw `new`/manual `free` pair.
- `std::format` for building strings (the HTTP request, error messages) instead of `+` concatenation or `sprintf`.
- `std::print` / `std::println` (C++23) instead of `std::cout <<`.
- Designated initializers (`addrinfo hints{.ai_family = ..., .ai_socktype = ...}`, `Credentials{.key_id = ..., .private_key_path = ...}`) so struct construction is self-documenting at the call site.
