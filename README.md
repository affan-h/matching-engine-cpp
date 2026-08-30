# Limit Order Book Matching Engine (C++)

A high-performance limit order book matching engine and non-blocking TCP gateway implemented in C++, modeled after the core infrastructure used in electronic trading systems. Built with a focus on deterministic latency, cache-efficient data structures, lock-free thread boundaries, and event-driven network I/O.

---

## Demo

### Terminal Dashboard

```
 L2 MATCHING ENGINE  Trades:7      11:32:01   [UP][DN] switch instrument   [,][.] scroll   q=quit

 SYMBOLS         MARKET DEPTH: AAPL                                              TIME & SALES
                   TYPE       PRICE      VOLUME                                   SIDE   QTY    PRICE    TIME
 > AAPL           ASK          103       10     ||||                              SELL   20     100      10:54:10
   RELIANCE       ASK          102       15     ||||||                            SELL   30      99      10:54:23
   INFY                 SPREAD: 2  |  MID: 101.0                                  BUY     3      98      10:54:50
   TATASTEEL      BID          100       30     ||||||||                          BUY    10      98      10:55:05
                  BID           99       30     ||||||||
                  BID           98       20     |||||

                 RESTING ORDERS  (cancel AAPL <ID>)
                   ORDER   SIDE    PRICE    ORIG     REM      FILL%
                   #7      SELL      98      120       40      67% *
                   #5      SELL     103       10       10       0%
                   #4      SELL     102       15       15       0%

 AAPL  |  VWAP: 99.30  |  Volume: 100  |  Cur Spread: 2  |  Avg Spread: 2.0  |  HiBid: 100  |  LoAsk: 98
 
 TERMINAL
 > sell AAPL 98 120
   System: sell placed. ID=7
```

### Interactive CLI

```
> register AAPL
Registered AAPL as instrument 0
> buy AAPL 100 50
BUY order placed. OrderId=1
> sell AAPL 100 30
(trade executed: 30 @ 100)
SELL order placed. OrderId=4
> ioc sell AAPL 99 100
IOC: 2 trade(s). ID=5
> fok buy AAPL 101 200
FOK rejected: insufficient liquidity.
> stats
AAPL:
  VWAP: 99.90   Volume: 100   AvgSpread: 1.00
```

---

## What It Does

When two traders place opposing orders at compatible prices, a matching engine pairs them and generates a trade. This project implements that system end-to-end — the data structures, matching logic, memory management, lock-free concurrency layer, non-blocking TCP network gateway, market data feed, and a live terminal dashboard for interacting with the engine in real time.

---

## Architecture

```
+------------------------------------------------------------------------------+
|                            EXTERNAL CLIENTS (TCP)                            |
|             Python Reference Client / Automated Ingestion Scripts            |
+------------------------------------------------------------------------------+
                                      │
                                      │ Binary TCP Frames (Big-Endian)
                                      ▼
+------------------------------------------------------------------------------+
|                  C++ TCP GATEWAY (Producer Thread - kqueue)                  |
|                                                                              |
|  - Non-blocking listening & client sockets via fcntl                         |
|  - Event-driven I/O multiplexing with macOS kqueue                           |
|  - Per-connection independent stream buffer & TcpParser state                |
|  - Deserialization & validation: framing, payload bounds, field ranges       |
|  - Bounded backpressure handling (bounded retries, connection protection)    |
+------------------------------------------------------------------------------+
                                      │
                                      │ Validated OrderEvent Structs
                                      ▼
+------------------------------------------------------------------------------+
|                   SPSC RING BUFFER (Lock-Free Thread Boundary)               |
|                       64K/1M slots, cacheline-aligned                        |
+------------------------------------------------------------------------------+
                                      │
                                      │ pop()
                                      ▼
+------------------------------------------------------------------------------+
|                MATCHING ENGINE CORE (Consumer Thread - Single Core)          |
|                                                                              |
|  - Strict single-threaded execution (zero mutexes / zero lock contention)   |
|  - Integer InstrumentId routing (AAPL, RELIANCE, INFY, TATASTEEL)            |
|  - Price Ladder: direct-indexed vector (O(1) price levels)                   |
|  - Bitmaps: __builtin_clzll / __builtin_ctzll for instantaneous best bid/ask |
|  - Intrusive doubly linked list: cacheline-local order queuing               |
|  - Memory Pool Allocator: pre-warmed free-list (zero heap allocations)       |
|  - O(1) Cancel via direct index array orderLookup[id]                        |
+------------------------------------------------------------------------------+
                                      │
                                      │ Trade & Snapshot Callbacks
                                      ▼
+------------------------------------------------------------------------------+
|                         DOWNSTREAM CONSUMERS & FEEDS                         |
|  - MarketDataFeed (L2 Snapshots)                                             |
|  - StatsTracker (Real-time VWAP, turnover, spread, and volume tracking)      |
|  - Live Dashboard / Console Visualizers                                      |
+------------------------------------------------------------------------------+
```

The network gateway and matching core run on separate threads connected by a lock-free Single-Producer Single-Consumer (`SPSCQueue`) ring buffer. The matching engine is strictly single-threaded — zero mutexes on the critical path. The market data feed decouples book state from downstream consumers via subscriber callbacks.

---

## Core Design Decisions

### Price Ladder (vector instead of std::map)

The order book uses a direct-indexed array where the index is the price. Access is $O(1)$ and cache-friendly. A `std::map` gives $O(\log n)$ with heap-allocated nodes scattered in memory.

Tradeoff: fixed price range (0–100,000) and higher upfront memory usage.

### Bitmap for Best Bid/Ask

A `uint64_t` bitmap tracks which price levels are active. Finding the best bid or ask uses `__builtin_clzll` / `__builtin_ctzll` — single CPU instructions. No scanning of empty levels.

### Intrusive Linked List

Each price level holds orders in a doubly linked list where `next/prev` pointers live inside the `Order` struct itself. This eliminates the separate node allocations that `std::list` requires and keeps orders at the same price level contiguous in pool memory.

### Memory Pool Allocator

A pre-allocated pool of 2 million `Order` slots replaces `new/delete` entirely. A free-list stack handles reuse after cancels. The matching loop makes zero heap allocations.

### O(1) Cancel via Direct Index

A `vector<Order*>` indexed by `OrderId` gives instant access to any resting order. Cancel is a pointer lookup, a linked list unlink, and a bitmap clear — no search required.

### Market Data Feed

The engine publishes an `L2Snapshot` (top 10 price levels per side) after every book change via a subscriber callback. Downstream consumers — stats tracker, CLI, dashboard — register handlers without the engine knowing about them. This mirrors real exchange market data dissemination architecture.

### Symbol Registry

A bidirectional map between string symbols (`"AAPL"`, `"RELIANCE"`) and integer `InstrumentId` values sits outside the hot path. The engine routes entirely by integer ID at runtime — zero string operations in the matching loop.

### Why Networking is Outside MatchingEngine

1. **Deterministic Latency**: Network I/O is subject to syscall overhead, socket buffer starvation, network jitter, client stalls, and TCP fragmentation. Isolating all socket handling to the gateway producer thread ensures the matching engine core runs uninterrupted at memory speed.
2. **Zero Locks on Hot Path**: The gateway thread and matching core communicate exclusively through a single-producer single-consumer (`SPSCQueue`) lock-free ring buffer. There are no mutexes, condition variables, or reader-writer locks in the matching loop.
3. **Decoupled Framing & Parsing**: All wire format decoding, validation, and framing recovery occur before events reach the queue. The engine consumes only clean, validated internal `OrderEvent` structs.

### Non-Blocking TCP Gateway (macOS kqueue)

The gateway uses BSD `kqueue` (`EVFILT_READ`) on non-blocking sockets (`O_NONBLOCK` via `fcntl`):
- **Incremental TCP Parsing**: Handles arbitrary stream chunking (e.g. 1-byte headers, split payloads, coalesced multiple frames).
- **Per-Client Connection State**: Each connected descriptor maintains an independent `TcpParser` and receive buffer. One client's partial stream never affects other clients.
- **Client Buffer Limits**: Enforces a strict 16 KB maximum buffer limit per connection to prevent memory exhaustion from slow/malformed clients.
- **Bounded Backpressure**: If the lock-free SPSC ring buffer becomes full, the gateway executes bounded retries (`std::this_thread::yield()`). If congestion persists, the gateway disconnects the saturated client rather than dropping orders silently or stalling indefinitely.

---

## Binary TCP Wire Protocol Specification

The gateway accepts an explicit length-prefixed binary wire format (Big-Endian / Network Byte Order).

### 1. Frame Header (3 Bytes)
```
+------------------------------------+-------------------------------------------+
|            Frame Header            |               Frame Payload               |
|  [2B Payload Length] [1B Msg Type] |            [Variable Payload]             |
+------------------------------------+-------------------------------------------+
|<------------- 3 Bytes ------------>|<----------- N Bytes (Length) ------------>|
```
- `payload_len` (`uint16_t`, BE): Length of payload in bytes (`MAX_PAYLOAD_LENGTH = 64`).
- `msg_type` (`uint8_t`):
  - `0x01`: New Limit Order (14-byte payload, 17-byte frame)
  - `0x02`: New Market Order (9-byte payload, 12-byte frame)
  - `0x03`: Cancel Order (12-byte payload, 15-byte frame)
  - `0x04`: Modify Order (20-byte payload, 23-byte frame)

### 2. Message Payloads

| Message Type | Fields (Big-Endian) | Constraints |
| :--- | :--- | :--- |
| **New Limit Order (`0x01`)** | `instrument_id` (`uint32`), `side` (`uint8`), `price` (`uint32`), `qty` (`uint32`), `tif` (`uint8`) | `side`: 0=Buy, 1=Sell<br>`price`: 1..100000<br>`qty` >= 1<br>`tif`: 0=GTC, 1=IOC, 2=FOK |
| **New Market Order (`0x02`)**| `instrument_id` (`uint32`), `side` (`uint8`), `qty` (`uint32`) | `side`: 0=Buy, 1=Sell<br>`qty` >= 1 |
| **Cancel Order (`0x03`)**    | `instrument_id` (`uint32`), `order_id` (`uint64`) | `order_id` >= 1 |
| **Modify Order (`0x04`)**    | `instrument_id` (`uint32`), `order_id` (`uint64`), `new_price` (`uint32`), `new_qty` (`uint32`) | `order_id` >= 1<br>`new_price`: 1..100000<br>`new_qty` >= 1 |

---

## Order Types

| Type | Behaviour |
|------|-----------|
| Limit GTC | Rests in book at specified price until filled or cancelled |
| Limit IOC | Fills available quantity immediately, residual discarded |
| Limit FOK | Fills entire quantity or cancelled with zero fills |
| Market    | Matches against best available price, no resting |

FOK uses a pre-match volume check — available liquidity at or better than the limit price is verified before any trades execute. If insufficient, the order is rejected atomically with no side effects.

---

## Benchmark Results

Measured on Azure Standard_F4als_v6 (4 vCores @ 3.69 GHz, 32MB L3 cache), Ubuntu 24.04, compiled with `g++ -O3`. Compared against a naive baseline using `std::map` + `std::queue`.

| Operation                       | Naive    | Optimized | Notes                     |
|--------------------------------|----------|-----------|---------------------------|
| Insert                         | 558 ns   | 992 ns    | See note below            |
| Match                          | 553 ns   | 583 ns    | Similar at low contention |
| Cancel (isolated)              | 582 ns   | 541 ns    | Marginal                  |
| Cancel (1000 resting orders)   | 1370 ns  | 535 ns    | **2.6x faster**           |

**On insert latency:** The optimized engine is slower on insert because its data structures (price ladder + orderLookup vector) are significantly larger than a `std::map`, causing cold cache misses on first access. This is a deliberate tradeoff — real matching engines experience far more cancel operations than inserts during volatile markets, so $O(1)$ cancel is the higher-value optimization.

**On cancel under contention:** The naive implementation scans a queue of N orders to find the target — $O(n)$. The optimized engine uses `orderLookup[id]` — $O(1)$ regardless of queue depth. The gap widens with the number of resting orders at a price level.

---

## Simulation Results

Multi-threaded simulation processing 5 million orders across 4 instruments (AAPL, RELIANCE, INFY, TATASTEEL), distributed via the SPSC ring buffer across a producer and consumer thread.

```
Orders submitted : 5,000,000
Trades executed  : 1,666,666

Per instrument (sample):
  AAPL      VWAP=150.00   volume=4,166,670   book updates=1,250,000
  RELIANCE  VWAP=2800.00  volume=4,166,660   book updates=1,250,000
  INFY      VWAP=1400.00  volume=4,166,660   book updates=1,250,000
  TATASTEEL VWAP=140.00   volume=4,166,670   book updates=1,250,000
```

---

## Test Suites

The project contains 54 automated test cases across 3 dedicated suites:

```
===== Matching Engine Test Suite =====
  PASS  test_limit_order_rests_in_book
  PASS  test_no_match_when_spread_exists
  PASS  test_full_fill
  PASS  test_partial_fill_remainder_rests
  PASS  test_price_priority
  PASS  test_fifo_same_price
  PASS  test_cancel_order
  PASS  test_cancel_nonexistent
  PASS  test_market_order_matches
  PASS  test_market_order_empty_book
  PASS  test_ioc_residual_discarded
  PASS  test_ioc_no_liquidity
  PASS  test_multi_level_match
  PASS  test_multi_instrument_isolation
  PASS  test_modify_size_down_keeps_priority
  PASS  test_fok_full_fill_executes
  PASS  test_fok_partial_fill_cancelled
  PASS  test_fok_no_liquidity
Results: 18 passed, 0 failed

===== Wire Protocol & TCP Parser Test Suite =====
  PASS  test_limit_order_round_trip
  PASS  test_market_order_round_trip
  PASS  test_cancel_order_round_trip
  PASS  test_modify_order_round_trip
  PASS  test_partial_header
  PASS  test_partial_payload
  PASS  test_one_byte_at_a_time
  PASS  test_multiple_frames_in_one_buffer
  PASS  test_multiple_frames_with_final_partial_frame
  PASS  test_wrong_payload_length
  PASS  test_payload_too_large
  PASS  test_unknown_message_type
  PASS  test_invalid_side
  PASS  test_invalid_tif
  PASS  test_zero_quantity
  PASS  test_invalid_price
  PASS  test_zero_order_id
  PASS  test_truncated_frame_disconnect
Results: 18 passed, 0 failed

===== TCP Gateway & kqueue Integration Test Suite =====
  PASS  test_server_starts_and_stops
  PASS  test_client_connects_and_disconnects
  PASS  test_limit_order_reaches_engine
  PASS  test_market_order_reaches_engine
  PASS  test_cancel_order_reaches_engine
  PASS  test_modify_order_reaches_engine
  PASS  test_fragmented_frame
  PASS  test_one_byte_at_a_time_frame
  PASS  test_multiple_frames_in_one_send
  PASS  test_multiple_frames_with_partial_final_frame
  PASS  test_malformed_frame
  PASS  test_oversized_frame
  PASS  test_unknown_message_type
  PASS  test_client_disconnect_handling
  PASS  test_disconnect_during_partial_frame
  PASS  test_multiple_simultaneous_clients
  PASS  test_queue_full_behavior
  PASS  test_buffer_overflow_protection
Results: 18 passed, 0 failed
```

---

## Key Design Tradeoffs

| Decision | Chosen | Alternative | Reason |
|---|---|---|---|
| Price structure | vector ladder | std::map | $O(1)$ vs $O(\log n)$, cache locality |
| Best price lookup | bitmap + CPU intrinsic | linear scan | Single instruction |
| Order storage | intrusive linked list | std::list | No separate node allocation |
| Memory | pool allocator | new/delete | Zero heap calls in hot path |
| Order lookup | direct index vector | unordered_map | True $O(1)$, no hashing |
| Threading | SPSC queue | mutex + shared queue | Zero contention, no lock overhead |
| Feed delivery | callback subscribers | polling | Decoupled, extensible |
| Symbol routing | integer InstrumentId | string map | No string ops in hot path |
| Network I/O | kqueue event loop | thread-per-client | Scalable non-blocking multiplexing |

---

## Complexity

| Operation    | Complexity | Bottleneck             |
|-------------|-----------|------------------------|
| Insert       | $O(1)$      | Cache miss on lookup   |
| Cancel       | $O(1)$      | Direct index           |
| Best bid/ask | $O(1)$      | Bitmap + intrinsic     |
| Match        | $O(k)$      | $k$ = orders matched     |
| FOK check    | $O(L)$      | $L$ = price levels scanned |
| Depth query  | $O(N)$      | $N$ = levels returned    |

---

## How to Build and Run

**Dependencies:** Google Benchmark (benchmarks only), C++17 compiler, Python 3.8+

**All Unit and Integration Tests:**
```bash
make test
```

**Run Benchmarks (requires Google Benchmark):**
```bash
make bench
```

**Multi-Instrument Simulation:**
```bash
make sim
```

**Interactive CLI:**
```bash
make cli
```

**Build and Run the TCP Gateway:**
```bash
make gateway
./gateway 12345
```

**Send Orders via Python Reference Client:**
```bash
python3 scripts/client.py --demo 12345
```

---

## File Structure

```
include/
  types.h               Order, Trade, L2Snapshot structs and type aliases
  price_level.h         PriceLevel: head/tail pointers + total volume
  order_pool.h          Pool allocator with free-list reuse
  orderbook.h           Order book interface
  matching_engine.h     Engine interface with feed and stats wiring
  network_protocol.h    OrderEvent struct (internal domain format)
  wire_protocol.h       Binary wire format constants, codecs, endian helpers
  tcp_parser.h          Incremental TCP stream parser and validation
  tcp_gateway.h         macOS kqueue gateway and connection management
  spsc_queue.h          Lock-free SPSC ring buffer
  symbol_registry.h     String symbol to InstrumentId mapping
  market_data.h         L2Snapshot type and MarketDataFeed subscriber
  stats_tracker.h       Per-instrument VWAP, spread, volume tracking

src/
  orderbook.cpp         Price ladder, bitmap, intrusive list, depth query
  matching_engine.cpp   Matching logic, order routing, feed publishing
  tcp_gateway.cpp       kqueue event loop, non-blocking I/O, SPSC handoff
  gateway_main.cpp      Standalone TCP gateway entry point

scripts/
  client.py             Python binary protocol encoder and TCP reference client

tests/
  dashboard.cpp         Live ncurses terminal dashboard
  cli.cpp               Interactive CLI — all order types
  simulation.cpp        Multi-threaded producer/consumer, 5M orders
  test_engine.cpp       18-case matching engine correctness suite
  test_wire_protocol.cpp 18-case wire protocol & parser test suite
  test_gateway.cpp      18-case TCP kqueue gateway integration test suite
  benchmark.cpp         Google Benchmark harness with naive baseline
```

---

## Notable Fixes During Development

1. **Volume Tracking on Partial Fill**: During stress testing, a partial fill left the order book in an inconsistent state: the matched order's quantity was decremented but the price level's `totalVolume` was not updated. Fixed by ensuring volume is decremented at the price level on every fill.
2. **Double GTC Insertion**: A double insertion bug caused GTC orders to be inserted twice into the order book after partial matches. Fixed by removing the unconditional insert that followed the TIF-gated insert.
3. **Benchmark Instrument Registration**: Fixed an uncaught `Unknown instrument id` runtime exception in benchmark runs by registering `"AAPL"` explicitly before order dispatch.
4. **Signal Safety on Closed Sockets (`SIGPIPE`)**: Writing to disconnected client sockets raised `SIGPIPE` (Exit 141). Resolved by setting `std::signal(SIGPIPE, SIG_IGN)` and configuring `SO_NOSIGPIPE` on macOS sockets.

---

## What This Is Not

This is an educational electronic exchange simulation and gateway, not a production trading system. It does not include persistent WAL recovery, FIX protocol compliance, multi-core sharding, or regulatory auditing.

---

## Known Limitations

- **Fixed Price Range (0–100,000)**: The vector price ladder requires a bounded discrete price space.
- **Single-Threaded Matching Core**: The engine itself is not thread-safe; thread boundary isolation is enforced via the SPSC queue.
- **macOS Event Loop**: The gateway uses BSD `kqueue`. A Linux build would require `epoll` or an abstraction layer.
- **In-Memory Only**: No persistent write-ahead logging (WAL) or snapshot replay across process restarts.
