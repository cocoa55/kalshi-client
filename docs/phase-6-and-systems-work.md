# Phase 6 and the systems pass: what changed and why

Everything from "implement Phase 6" through the end of the performance and hardening pass, in the order it happened.

## TL;DR

- **Phase 6:** an autonomous trading bot that runs indefinitely, reconnects on its own, and enforces hard risk limits. It was tested end to end on Kalshi's demo exchange.
- **Systems pass:** the project's focus is low-level C++, not trading, so six upgrades followed. Each was measured before and after.

| | Before | After |
|---|---:|---:|
| Decode an order-book delta | 2.29 µs | **0.75 µs** (3.1×) |
| Decode a 40-level snapshot | 23.2 µs | **6.8 µs** (3.4×) |
| Apply a book update + read top of book | 196 ns | **97 ns** (2×) |
| Parse a price | 80 ns | **12 ns** (6.7×) |
| Sign a request | 903 µs | **531 µs** (1.7×) |
| Send an order over HTTPS | 249 ms | **82 ms** (no handshake per order) |

- **Testing:** 45 unit tests, a clean run under AddressSanitizer + UndefinedBehaviorSanitizer, and four property-checking fuzzers (~30M inputs, no failures).
- **Bugs fixed:** 12 real bugs, listed in [Bugs found and fixed](#bugs-found-and-fixed). One was a hang that could eat all memory, found by fuzzing the old JSON lexer.

---

## Part 1: Phase 6, the trading bot

### The strategy (deliberately simple)

`MeanReversionStrategy` in `src/trading/strategy.cpp`:

1. **Fair value** is an exponential moving average of the mid price, weighted by elapsed time (τ = 60 s) rather than message count, so a burst of updates can't skew it.
2. **Signal:** buy when the best ask is ≥ 3¢ below fair value; sell when the best bid is ≥ 3¢ above it.
3. **Filters:** no trading during a 60 s warmup, on one-sided or crossed books, or when the spread is over 4¢.
4. **Risk:** ±5 contract cap, 1 contract per order, 5 s cooldown.
5. **Immediate-or-cancel orders only:** nothing rests on the book, so the bot needs no cancel logic and a disconnect can't leave stale orders behind.

The strategy is pure decision logic with no I/O, which is why it's easy to unit test.

### The bot loop

- Runs until `SIGINT`/`SIGTERM`, and reconnects with exponential backoff (1 s → 60 s).
- **Fill race guard:** an order's REST response arrives before its fill on the WebSocket. Until the WebSocket confirms a fill, it still counts toward the position limit. Without this, a second order could slip past the cap during that gap.

### Testing it on the demo exchange

| Symptom | Cause | Fix |
|---|---|---|
| Bot printed nothing after subscribing | Quiet market; status only printed on data messages | Expected behavior, not a bug |
| No trades on a market stuck at 59¢/60¢ | Price never moved 3¢ from fair value | Forced a smoke test with a temporary `entry_edge{-1}` |
| `Missing fields in order response` | The error hid the actual response | Error now includes the raw response body |
| `insufficient_shard_balance` | Kalshi keeps a separate balance per matching-engine shard; baseball is on shard 3, the funds were on shard 0 | Added `--fund-shard <index> <dollars>` and moved $100 |

**Smoke test result:** 5 buys filled at 62¢. Each fill arrived on the WebSocket and matched its order. Exposure counted the not-yet-confirmed fill (`position 0 exposure 1`). The ±5 limit stopped further buys. Sells were tracked correctly too. `entry_edge` was then put back to `3`.

### Repo housekeeping

- Removed leftover merge-conflict markers from the README.
- Rebased Phase 6 onto README edits made on GitHub.
- Rewrote one pushed commit to remove a `Co-Authored-By: Claude` trailer, so the repo shows a single contributor. **This needs a force-push from your machine.**

---

## Part 2: The systems pass

Benchmarks were recorded first, against the unmodified code, so every change has a real before and after (`bench/benchmarks.cpp`, Release build, median of 5 runs).

### 1. Fixed-array order book

**Before:** `std::unordered_map<Price, Quantity>` per side, with a full scan for the best price on every read.
**After:** prices can only be 1–99¢, so each side is a `std::array<Quantity, 101>` indexed by price. The best bid is maintained as levels change; only removing the best level triggers a short scan downward.

- No hashing and no allocation; the whole book is about 1.6 KB, so it stays in L1 cache.
- A snapshot is parsed into scratch arrays first, so a malformed one leaves the current book untouched.
- A level going negative means the local book has drifted from the exchange's. That's now reported, and the bot resubscribes for a fresh snapshot.

**196 → 97 ns** per update + top-of-book read.

### 2. Integer price parsing

**Before:** `static_cast<int64_t>(std::stod(s) * 100)`, which turned `"0.29"` into 28 (28.999… truncated).
**After:** `parse_fixed<Scale>()` in `include/fixed_point.hpp` does integer arithmetic only, is `constexpr` with compile-time `static_assert` tests, and has exact overflow checks. Sub-cent or otherwise lossy input is an error, not silently rounded.

**80 → 12 ns**, and correct.

### 3. Persistent TLS connection for orders

**Before:** every order did DNS + TCP + a full TLS handshake, sent `Connection: close`, read until EOF, and re-read the private key from disk to sign.
**After:**

- **`HttpClient`** keeps one connection open (HTTP/1.1 keep-alive). A zero-timeout `poll()` detects when the server has closed an idle connection, and the bot sends a keep-warm request when idle, so orders never pay for a handshake.
- **Retry policy:** a failed *send* on a reused connection is retried once, which is safe because nothing reached the server. A failed *read* is never retried, because the order may have executed.
- **A real HTTP/1.1 response parser** (`http_response.cpp`). It's a pure function, so it can be fuzzed. It handles `Content-Length`, chunked bodies with extensions and trailers, `Connection` semantics, HTTP/1.0, and size limits, and it rejects conflicting `Content-Length` headers.
- **`Signer`** parses the PEM once at startup and builds all three auth headers. The WebSocket and HTTP code share it.
- **TCP:** connect now has a 5 s deadline (non-blocking `connect` + `poll`); before, a blackholed connect blocked for about 2 minutes. `TCP_NODELAY` is also set.

**249 → 82 ms** per request. One connection served 25 requests in the benchmark. Signing: **903 → 531 µs**.

### 4. Tests, sanitizers and fuzzing

- **45 unit tests** (`tests/`) using a ~40-line self-registering framework with no dependencies.
- **Sanitizers:** `-DKALSHI_SANITIZE=ON` builds everything with ASan + UBSan, and the suite runs clean.
- **Fuzzing:** four libFuzzer targets (`-DKALSHI_FUZZ=ON`, Clang). Each checks a property, not just "no crash":

| Fuzzer | Property |
|---|---|
| `fuzz_json` | print → parse → print round-trips unchanged |
| `fuzz_decoder_differential` | fast decoder ≡ JSON-tree decoder on every input |
| `fuzz_frame_parser` | never over-reads; rebuild → reparse preserves payload |
| `fuzz_http_response` | consistent when bytes arrive in pieces |

**Proving the harness works:** pointed at the *old* JSON lexer, the fuzzer ran out of memory within its first few dozen inputs. Minimal reproduction: `{"position":-3}`. Any `-` outside a string entered the number branch, but the scan loop only advanced over digits and `.`, so it appended empty tokens forever. Kalshi quotes its order-book deltas, so the bot never hit it, but a bare negative number in any REST response (for example, a short position) would have hung the bot. It's now a regression test.

### 5. JSON: allocation-free lexer, then skipping the tree

This item needed measurement to get right:

1. **Allocation-free lexer:** tokens became `string_view`s into the receive buffer, and the parser pulls them one at a time with no token vector. **The result: no speedup.** Almost every Kalshi token fits in `std::string`'s small-string buffer, so the old lexer wasn't really allocating.
2. **Measure:** timing each stage separately showed the generic JSON tree (`flat_map` per object, `vector` per array) was ~13 of 24 µs. `const` struct members were also silently turning moves into copies.
3. **Decode straight from tokens:** `kalshi_decoder.cpp` walks the token stream once, keeps `string_view`s for just the fields it needs, and builds the typed struct directly. It still validates the whole payload. It mirrors the tree path's rules exactly (first duplicate key wins, numbers accepted as strings, same depth limit), and the differential fuzzer proves it. Payloads with escaped strings fall back to the tree path.
4. **Cheap error type:** returning `std::expected<Token, std::string>` per token made every return non-trivial. A small trivially copyable `JsonError{what, offset}` made lexing **37% faster**.
5. **Tried and dropped:** `memchr` (SIMD) string scanning. Kalshi's strings are ~6 characters long, so call overhead cancelled the gain.

**Delta 2.29 → 0.75 µs. Snapshot 23.2 → 6.8 µs.**

The parser also became correct: escapes (including `\"` and UTF-16 surrogate pairs), strict number grammar, rejection of trailing garbage and unterminated objects, and a nesting limit so hostile input can't overflow the stack.

### 6. Latency measurement

`LatencyStats` records into a fixed ring buffer (bounded memory, a store and an increment per sample) and prints p50/p90/p99/max on shutdown for four stages:

| Stage | Measures |
|---|---|
| decode | frame received → typed message |
| book + strategy | message → trading decision |
| tick-to-order-sent | frame received → order bytes handed to the kernel |
| order round trip | order sent → response parsed |

The CPU-side numbers come from benchmarks; the network-side ones only exist on a live run, so run the bot to see them.

### 7. `epoll` event loop: skipped, as requested

The bot stays single-threaded with blocking I/O. The trade-off to be ready to discuss is that an order's network round trip blocks market-data processing while it's in flight.

---

## Bugs found and fixed

| # | Bug | Impact | How it was found |
|---|---|---|---|
| 1 | `stod * 100` truncation (`"0.29"` → 28¢) | Every price off by a cent | Code reading |
| 2 | `TcpSocket` never closed its fd | Leaked one socket per order | Code reading |
| 3 | No receive timeout | Silent disconnect hung the bot forever | Code reading |
| 4 | No connect timeout | Blackholed connect blocked ~2 min | Code reading |
| 5 | `SIGPIPE` not ignored | Writing to a reset connection killed the process | Code reading |
| 6 | Lexer infinite loop on a bare negative number | Hang + out-of-memory | **Fuzzing** |
| 7 | Lexer ended strings at an escaped `\"` | Wrong strings | Code reading → test |
| 8 | Parser read past the token array on input like `{` | Out-of-bounds read | Code reading → test |
| 9 | Unterminated objects silently accepted | Corrupt data accepted | Code reading → test |
| 10 | No nesting limit | Stack overflow on deep input | Code reading → test |
| 11 | Frame header could claim a 2⁶³-byte payload | Unbounded buffer growth | Code reading → test |
| 12 | `parse_fixed` overflow guard off by one digit | Rejected valid values near `INT64_MAX` | **Unit test** |

The bot also now detects missed order-book messages (sequence gaps) and resubscribes. Before, a missed message meant trading on a wrong book.

---

## C++23 features used

Applied where they make the code clearer. Each row names the spot:

| Feature | Where |
|---|---|
| `std::expected` + `and_then` / `transform` / `transform_error` | Error handling throughout; e.g. `parse_delta(obj).transform(make)` |
| `std::optional::and_then` / `transform` | `f.sid.value.and_then(parse_u64)`, price formatting |
| `std::print` / `std::println` | All output |
| `std::flat_map` | JSON objects |
| `std::ranges::fold_left`, `std::views::values` | Exposure = position + unconfirmed fills |
| `std::ranges::contains`, `contains_subrange` | Opcode validation, case-insensitive header matching |
| `std::views::enumerate` | JSON printing |
| `std::string::resize_and_overwrite` | Base64 straight into the string, no zero-fill or extra copy |
| `std::string::contains` | Tests |
| `std::to_underlying` | Opcode → byte |
| `static operator()` | Stateless OpenSSL deleters |
| `std::byteswap`, `std::unreachable`, `uz` literals | Frame lengths, exhaustive switches, sizes |

**Where it was deliberately not used:** `views::transform | ranges::to` measured ~10% slower on the snapshot hot path (construct then move vs. in place), so that spot stays a plain loop, with a comment saying why. The lexer and parser loops keep explicit `if (!x) return` checks, because monadic chains don't fit loops.

---

## How to verify

```sh
cmake -S . -B cmake-build-debug && cmake --build cmake-build-debug && ./cmake-build-debug/unit_tests
CXX=clang++ cmake -S . -B cmake-build-asan -DKALSHI_SANITIZE=ON && cmake --build cmake-build-asan && ./cmake-build-asan/unit_tests
CXX=clang++ cmake -S . -B cmake-build-fuzz -DKALSHI_FUZZ=ON && cmake --build cmake-build-fuzz
mkdir -p fuzz/work/decoder && ./cmake-build-fuzz/fuzz_decoder_differential -max_total_time=60 -artifact_prefix=fuzz/work/ fuzz/work/decoder fuzz/corpus/decoder
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release && ./cmake-build-release/benchmarks --network
```

**Not yet verified live:** the refactored WebSocket and order path. It compiles, is unit tested, and the HTTP client was exercised against the real demo API, but the authenticated bot hasn't been rerun since the refactor. Run it once on a demo market and check the latency summary it prints on Ctrl+C.

## Known limitations

- Single market; the bot assumes a flat position at startup.
- Blocking I/O on one thread (see item 7).
- Fragmented WebSocket messages aren't reassembled (Kalshi doesn't send them).
- The sequence-gap check assumes Kalshi's `seq` increments by one per order-book message. If a live run shows constant resubscribes, that assumption is wrong.

## Interview talking points

- **Measure first:** the allocation-free lexer did *nothing*, and only profiling showed the tree was the real cost. That's a better story than any single speedup.
- **Differential fuzzing** is how you can be sure an optimized decoder is *exactly* equivalent to the simple one.
- **Order safety:** retry a failed send, never a failed read. Count unconfirmed fills toward the position limit. Use immediate-or-cancel so a crash leaves nothing resting on the book.
- **Small, sharp systems details:** `SIGPIPE`, `TCP_NODELAY`, connect deadlines, detecting dead keep-alive connections with `poll`, and a book that fits in L1.
