# Limit Order Book Matching Engine (C++)

A high-performance limit order book matching engine, non-blocking TCP gateway, and client-facing Python REST API implemented in C++ and Python, modeled after the core infrastructure used in electronic trading systems. Built with a focus on deterministic latency, cache-efficient data structures, lock-free thread boundaries, event-driven network I/O, and clean API separation.

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

When two traders place opposing orders at compatible prices, a matching engine pairs them and generates a trade. This project implements that system end-to-end — the data structures, matching logic, memory management, lock-free concurrency layer, non-blocking TCP network gateway, client-facing HTTP REST API, market data feed, and a live terminal dashboard for interacting with the engine in real time.

---

## Architecture

```
+------------------------------------------------------------------------------+
|                  HTTP REST CLIENTS (Web / Python / curl)                     |
+------------------------------------------------------------------------------+
                                      │
                                      │ HTTP / JSON
                                      ▼
+------------------------------------------------------------------------------+
|                   PYTHON API SERVICE (FastAPI / Pydantic)                    |
|                                                                              |
|  - Request validation: symbols, price ranges, quantities, side, TIF          |
|  - REST endpoints: POST /orders, DELETE /orders, PATCH /orders, GET /book    |
|  - Active gateway probe: GET /health                                         |
|  - Thread-safe binary TCP client with automatic reconnect & framing          |
+------------------------------------------------------------------------------+
                                      │
                                      │ Binary TCP Frames (Big-Endian Wire Protocol)
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

The system is partitioned into clear architectural boundaries:
1. **HTTP / REST Layer**: High-level validation and client integration via FastAPI.
2. **TCP Gateway Layer**: Event-driven I/O multiplexing via macOS `kqueue`.
3. **Lock-Free Queue Boundary**: Single-Producer Single-Consumer (`SPSCQueue`) ring buffer.
4. **Matching Engine Core**: Deterministic, zero-allocation matching loop running at memory speed on a dedicated thread.

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

### Why Networking & HTTP are Outside MatchingEngine

1. **Deterministic Latency**: Network I/O and HTTP request parsing are subject to syscall overhead, socket buffer starvation, network jitter, client stalls, and TCP fragmentation. Isolating all networking to outer adapter layers ensures the matching engine core runs uninterrupted.
2. **Zero Locks on Hot Path**: The gateway thread and matching core communicate exclusively through a single-producer single-consumer (`SPSCQueue`) lock-free ring buffer. There are no mutexes, condition variables, or reader-writer locks in the matching loop.
3. **Decoupled Framing & Parsing**: All wire format decoding, validation, and framing recovery occur before events reach the queue. The engine consumes only clean, validated internal `OrderEvent` structs.

### Non-Blocking TCP Gateway (macOS kqueue)

The gateway uses BSD `kqueue` (`EVFILT_READ`) on non-blocking sockets (`O_NONBLOCK` via `fcntl`):
- **Incremental TCP Parsing**: Handles arbitrary stream chunking (e.g. 1-byte headers, split payloads, coalesced multiple frames).
- **Per-Client Connection State**: Each connected descriptor maintains an independent `TcpParser` and receive buffer. One client's partial stream never affects other clients.
- **Client Buffer Limits**: Enforces a strict 16 KB maximum buffer limit per connection to prevent memory exhaustion from slow/malformed clients.
- **Bounded Backpressure**: If the lock-free SPSC ring buffer becomes full, the gateway executes bounded retries (`std::this_thread::yield()`). If congestion persists, the gateway disconnects the saturated client rather than dropping orders silently or stalling indefinitely.

---

## Client-Facing Python REST API Service

The Python API service (`FastAPI` + `Pydantic`) provides a standard HTTP interface while speaking the binary wire protocol to the C++ Gateway over TCP.

### Endpoints

| Method | Path | Description | Status Code |
| :--- | :--- | :--- | :--- |
| `POST` | `/orders` | Submit Limit (GTC/IOC/FOK) or Market order | `202 Accepted` |
| `DELETE` | `/orders/{order_id}` | Cancel resting order by ID (`?symbol=AAPL`) | `200 OK` |
| `PATCH` | `/orders/{order_id}` | Modify resting order price and quantity | `200 OK` |
| `GET` | `/book/{symbol}` | Query current order book depth for symbol | `200 OK` |
| `GET` | `/health` | Active probe checking API & C++ Gateway reachability | `200 OK` / `503` |

### Example REST Requests

#### 1. Submit Limit Buy Order
```bash
curl -X POST http://127.0.0.1:8000/orders \
  -H "Content-Type: application/json" \
  -d '{"symbol": "AAPL", "side": "buy", "order_type": "limit", "price": 150, "quantity": 10, "time_in_force": "GTC"}'
```
Response (`202 Accepted`):
```json
{
  "status": "ACCEPTED",
  "symbol": "AAPL",
  "instrument_id": 0,
  "side": "buy",
  "order_type": "limit",
  "price": 150,
  "quantity": 10,
  "time_in_force": "GTC",
  "message": "Limit order successfully submitted to matching engine gateway"
}
```

#### 2. Submit Market Sell Order
```bash
curl -X POST http://127.0.0.1:8000/orders \
  -H "Content-Type: application/json" \
  -d '{"symbol": "INFY", "side": "sell", "order_type": "market", "quantity": 50}'
```

#### 3. Cancel an Order
```bash
curl -X DELETE "http://127.0.0.1:8000/orders/1?symbol=AAPL"
```

#### 4. Modify an Order
```bash
curl -X PATCH http://127.0.0.1:8000/orders/1 \
  -H "Content-Type: application/json" \
  -d '{"symbol": "AAPL", "new_price": 155, "new_quantity": 5}'
```

#### 5. Check Health
```bash
curl http://127.0.0.1:8000/health
```
Response (`200 OK`):
```json
{
  "status": "healthy",
  "gateway": {
    "host": "127.0.0.1",
    "port": 12345,
    "connected": true,
    "error": null
  },
  "symbols": ["AAPL", "RELIANCE", "INFY", "TATASTEEL"]
}
```

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

The project contains 69 automated test cases across C++ and Python suites:

### 1. C++ Engine & Gateway Tests (54 tests)
```bash
make test
```
- **`test_engine` (18 tests)**: Core matching semantics (FIFO priority, price priority, GTC/IOC/FOK execution, partial fills, cancellations, multi-instrument isolation).
- **`test_wire` (18 tests)**: Protocol framing, endian-safe serialization/deserialization, frame length validation, boundary conditions, malformed frame detection.
- **`test_gateway` (18 tests)**: Real TCP socket integration via kqueue (server lifecycle, client connections, fragmented TCP frames, 1-byte delivery, multiple clients, backpressure, buffer overflow).

### 2. Python REST API Tests (15 tests)
```bash
make test-api
```
- **`test_api` (15 tests)**: HTTP endpoint validation, Pydantic bounds checks, invalid symbols/sides/prices, gateway unreachable handling, health probes, and end-to-end HTTP -> FastAPI -> TCP Client -> C++ Gateway -> Matching Engine execution.

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
| Client API | FastAPI REST adapter | direct HTTP in C++ | Decouples web/JSON from matching core |

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

### Prerequisites
- C++17 compiler (Clang / Apple LLVM or GCC)
- Google Benchmark (`brew install google-benchmark`, for benchmarks only)
- Python 3.8+ (for API service and test client)

### Build Targets

```bash
# 1. Run all C++ unit and integration test suites
make test

# 2. Run Python REST API unit and integration test suites
make test-api

# 3. Run Google Benchmark latency suite
make bench

# 4. Run multi-threaded 5M order simulation
make sim

# 5. Run interactive terminal CLI
make cli

# 6. Build the C++ TCP Gateway binary
make gateway
```

### Running the End-to-End System

#### Step 1: Start the C++ TCP Gateway
In terminal 1:
```bash
./gateway 12345
```

#### Step 2: Start the Python REST API Service
In terminal 2:
```bash
make api
```
The FastAPI application starts on `http://127.0.0.1:8000`.

#### Step 3: Send Orders via HTTP
In terminal 3:
```bash
# Submit Buy Limit order
curl -X POST http://127.0.0.1:8000/orders \
  -H "Content-Type: application/json" \
  -d '{"symbol": "AAPL", "side": "buy", "order_type": "limit", "price": 150, "quantity": 10, "time_in_force": "GTC"}'

# Submit matching Sell Limit order
curl -X POST http://127.0.0.1:8000/orders \
  -H "Content-Type: application/json" \
  -d '{"symbol": "AAPL", "side": "sell", "order_type": "limit", "price": 150, "quantity": 10, "time_in_force": "GTC"}'
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

api/
  __init__.py           API module definition
  config.py             Gateway host/port settings & symbol mapping
  models.py             Pydantic models for order requests/responses
  tcp_client.py         Thread-safe binary TCP client for C++ gateway
  main.py               FastAPI application, routes, and exception handlers

scripts/
  client.py             Python binary protocol encoder and TCP reference client

tests/
  dashboard.cpp         Live ncurses terminal dashboard
  cli.cpp               Interactive CLI — all order types
  simulation.cpp        Multi-threaded producer/consumer, 5M orders
  test_engine.cpp       18-case matching engine correctness suite
  test_wire_protocol.cpp 18-case wire protocol & parser test suite
  test_gateway.cpp      18-case TCP kqueue gateway integration test suite
  test_api.py           15-case FastAPI and end-to-end integration test suite
  benchmark.cpp         Google Benchmark harness with naive baseline
```

---

## Notable Fixes During Development

1. **Volume Tracking on Partial Fill**: During stress testing, a partial fill left the order book in an inconsistent state: the matched order's quantity was decremented but the price level's `totalVolume` was not updated. Fixed by ensuring volume is decremented at the price level on every fill.
2. **Double GTC Insertion**: A double insertion bug caused GTC orders to be inserted twice into the order book after partial matches. Fixed by removing the unconditional insert that followed the TIF-gated insert.
3. **Benchmark Instrument Registration**: Fixed an uncaught `Unknown instrument id` runtime exception in benchmark runs by registering `"AAPL"` explicitly before order dispatch.
4. **Signal Safety on Closed Sockets (`SIGPIPE`)**: Writing to disconnected client sockets raised `SIGPIPE` (Exit 141). Resolved by setting `std::signal(SIGPIPE, SIG_IGN)` and configuring `SO_NOSIGPIPE` on macOS sockets.
5. **Socket Port Reuse (`SO_REUSEPORT`)**: Enabled `SO_REUSEPORT` alongside `SO_REUSEADDR` to eliminate port binding collisions during rapid test execution.

---

## What This Is Not

This is an educational electronic exchange simulation, gateway, and REST adapter, not a production trading system. It does not include persistent WAL recovery, FIX protocol compliance, multi-core sharding, or regulatory auditing.

---

## Known Limitations

- **Fixed Price Range (0–100,000)**: The vector price ladder requires a bounded discrete price space.
- **Single-Threaded Matching Core**: The engine itself is not thread-safe; thread boundary isolation is enforced via the SPSC queue.
- **macOS Event Loop**: The gateway uses BSD `kqueue`. A Linux build would require `epoll` or an abstraction layer.
- **In-Memory Only**: No persistent write-ahead logging (WAL) or snapshot replay across process restarts.
