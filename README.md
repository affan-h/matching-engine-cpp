# Limit Order Book Matching Engine (C++)

A high-performance limit order book matching engine, non-blocking TCP gateway, in-memory read model / projector query plane, and client-facing Python REST API implemented in C++ and Python, modeled after the core infrastructure used in electronic trading systems. Built with a focus on deterministic latency, cache-efficient data structures, lock-free thread boundaries, event-driven network I/O, CQRS command/query plane separation, and clean API isolation.

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

When two traders place opposing orders at compatible prices, a matching engine pairs them and generates a trade. This project implements that system end-to-end — the data structures, matching logic, memory management, lock-free concurrency layer, non-blocking TCP network gateway, event projection pipeline, in-memory read model, client-facing HTTP REST API, market data feed, and a live terminal dashboard for interacting with the engine in real time.

---

## Architecture (Command & Query Plane Separation)

```
                              COMMAND PLANE (Ingress)
+-----------------------------------------------------------------------------------------+
|                        HTTP CLIENTS (Web / Python / curl)                               |
+-----------------------------------------------------------------------------------------+
                                           │
                                           │ POST /orders, DELETE /orders, PATCH /orders
                                           ▼
+-----------------------------------------------------------------------------------------+
|                         PYTHON API SERVICE (FastAPI / Pydantic)                         |
|  - Request validation: symbols, price ranges, quantities, side, TIF                     |
|  - Thread-safe binary TCP client with automatic reconnect & frame serialization         |
+-----------------------------------------------------------------------------------------+
                                           │
                                           │ Binary TCP Frames (0x01..0x04)
                                           ▼
+-----------------------------------------------------------------------------------------+
|                        C++ TCP GATEWAY (macOS kqueue Event Loop)                        |
|  - Non-blocking listening & client sockets (O_NONBLOCK via fcntl)                       |
|  - Incremental stream parsing (TcpParser) & per-connection state buffers                |
|  - Bounded backpressure handling and saturation protection                              |
+-----------------------------------------------------------------------------------------+
                                           │
                                           │ OrderEvent Structs
                                           ▼
+-----------------------------------------------------------------------------------------+
|                  COMMAND SPSC RING BUFFER (Lock-Free Thread Boundary)                   |
|                             64K slots, cacheline-aligned                                |
+-----------------------------------------------------------------------------------------+
                                           │
                                           │ pop()
                                           ▼
+-----------------------------------------------------------------------------------------+
|                   MATCHING ENGINE CORE (Dedicated Single Thread - Hot Path)             |
|  - Zero mutexes / zero locks / zero heap allocations on execution hot path              |
|  - Price Ladder: direct-indexed vector (O(1) price levels)                              |
|  - Bitmaps: __builtin_clzll / __builtin_ctzll for instantaneous best bid/ask            |
|  - Intrusive doubly linked list & pre-warmed memory pool allocator                      |
|  - Emits fixed-size POD outbound events (Trade, L2Update, OrderState)                   |
+-----------------------------------------------------------------------------------------+
                                           │
                                           │ Fixed-size POD OutboundEvent
                                           ▼
+-----------------------------------------------------------------------------------------+
|                  OUTBOUND SPSC RING BUFFER (Lock-Free Thread Boundary)                  |
|          64K slots, acquire/release ordering, backpressure & L2 coalescing              |
+-----------------------------------------------------------------------------------------+
                                           │
                                           │ pop()
                                           ▼
+-----------------------------------------------------------------------------------------+
|                    READ MODEL PROJECTOR (Dedicated Consumer Thread)                     |
|  - Continuously drains outbound event queue                                             |
|  - Applies events to in-memory ReadModel under write lock                               |
+-----------------------------------------------------------------------------------------+
                                           │
                                           │ update
                                           ▼
+-----------------------------------------------------------------------------------------+
|                         READ MODEL / QUERY PLANE (In-Memory)                            |
|                                                                                         |
|  - L2 Book Snapshots: latest top 10 bids/asks per instrument + sequence version         |
|  - Bounded Trade History: fixed circular buffer (1000 trades/symbol, FIFO eviction)     |
|  - Bounded Order History: tracked order states (10,000 orders, FIFO eviction)           |
|  - Query Synchronization: std::shared_mutex (concurrent multi-reader access)            |
+-----------------------------------------------------------------------------------------+
                                           │
                                           │ std::shared_lock (Cold Query Path)
                                           ▼
+-----------------------------------------------------------------------------------------+
|                 QUERY PLANE REST ENDPOINTS (FastAPI GET /book, /trades, /orders)        |
+-----------------------------------------------------------------------------------------+
```

The system strictly enforces the CQRS (Command Query Responsibility Segregation) pattern:
1. **Execution Hot Path (Zero Mutexes, Zero Allocations)**: `MatchingEngine` produces execution events directly into a lock-free SPSC ring buffer. It never performs socket I/O, never acquires query locks, and never touches FastAPI.
2. **Projection Pipeline**: A dedicated `Projector` background thread consumes from the outbound queue and updates the `ReadModel`.
3. **Query Path (Cold Path Synchronization)**: REST query endpoints read from `ReadModel` using `std::shared_lock<std::shared_mutex>`. Multiple readers query concurrently without blocking each other and without affecting the matching engine.

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

### Outbound Events & Bounded SPSC Queue

Outbound events (`TradeEventPayload`, `L2UpdateEventPayload`, `OrderStateEventPayload`) are packaged into fixed-size POD structs with zero dynamic allocation (`std::string` and `std::vector` are completely avoided on the matching thread).
- **Memory Ordering**: Uses `std::memory_order_release` on writes and `std::memory_order_acquire` on reads.
- **Full-Queue / Backpressure Policy**:
  - `Trade` and `OrderState` execution events are non-droppable and use bounded yield retries to guarantee history consistency.
  - `L2Update` snapshots are coalesced/dropped if congested, as the subsequent snapshot contains the superseding latest book state.

### Bounded In-Memory Read Model

- **L2 Book State**: Retains the latest depth snapshot per instrument (top 10 levels, sequence number, timestamp).
- **Trade History**: Fixed circular buffer (`BoundedTradeHistory`) capped at 1,000 trades per symbol. When capacity is reached, new trades overwrite the oldest entries (FIFO eviction).
- **Order State History**: Bounded lookup table capped at 10,000 orders with automatic eviction of oldest orders.

### Asymmetric Concurrency Model

- **Hot Path**: Matching Engine $\to$ Lock-free SPSC $\to$ Projector. No mutexes or locks.
- **Cold Path**: Client Query $\to$ `std::shared_lock<std::shared_mutex>` $\to$ ReadModel. Multiple concurrent queries execute in parallel without lock contention.

---

## Client-Facing Python REST API Service

The Python API service (`FastAPI` + `Pydantic`) provides a standard HTTP interface while speaking the binary wire protocol to the C++ Gateway over TCP.

### Endpoints

| Method | Path | Plane | Description | Status Code |
| :--- | :--- | :--- | :--- | :--- |
| `POST` | `/orders` | Command | Submit Limit (GTC/IOC/FOK) or Market order | `202 Accepted` |
| `DELETE` | `/orders/{order_id}` | Command | Cancel resting order by ID (`?symbol=AAPL`) | `200 OK` |
| `PATCH` | `/orders/{order_id}` | Command | Modify resting order price and quantity | `200 OK` |
| `GET` | `/book/{symbol}` | Query | Query current L2 order book depth snapshot | `200 OK` / `404` |
| `GET` | `/trades/{symbol}` | Query | Query recent trade execution history (`?limit=50`) | `200 OK` / `404` |
| `GET` | `/orders/{order_id}` | Query | Query order lifecycle status and fill quantities | `200 OK` / `404` |
| `GET` | `/health` | Ops | Active probe checking API & C++ Gateway reachability | `200 OK` / `503` |

### Example REST Requests

#### 1. Submit Limit Buy Order (Command Plane)
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

#### 2. Query Order Book Depth (Query Plane)
```bash
curl http://127.0.0.1:8000/book/AAPL
```
Response (`200 OK`):
```json
{
  "symbol": "AAPL",
  "instrument_id": 0,
  "sequence": 1,
  "timestamp": 1725012345678,
  "bids": [{"price": 150, "quantity": 10}],
  "asks": []
}
```

#### 3. Query Trade History (Query Plane)
```bash
curl "http://127.0.0.1:8000/trades/AAPL?limit=10"
```
Response (`200 OK`):
```json
{
  "symbol": "AAPL",
  "instrument_id": 0,
  "trades": [
    {
      "trade_id": 1,
      "symbol": "AAPL",
      "buy_order_id": 1,
      "sell_order_id": 2,
      "price": 150,
      "quantity": 10,
      "aggressor_side": "sell",
      "timestamp": 1725012345690
    }
  ]
}
```

#### 4. Query Order Status (Query Plane)
```bash
curl http://127.0.0.1:8000/orders/1
```
Response (`200 OK`):
```json
{
  "order_id": 1,
  "symbol": "AAPL",
  "instrument_id": 0,
  "side": "buy",
  "price": 150,
  "original_quantity": 10,
  "remaining_quantity": 0,
  "filled_quantity": 10,
  "status": "FILLED",
  "timestamp": 1725012345690
}
```

---

## Binary TCP Wire Protocol Specification

The gateway accepts an explicit length-prefixed binary wire format (Big-Endian / Network Byte Order) for both Commands and Queries.

### 1. Frame Header (3 Bytes)
```
+------------------------------------+-------------------------------------------+
|            Frame Header            |               Frame Payload               |
|  [2B Payload Length] [1B Msg Type] |            [Variable Payload]             |
+------------------------------------+-------------------------------------------+
|<------------- 3 Bytes ------------>|<----------- N Bytes (Length) ------------>|
```

### 2. Message Types & Framing

| Code | Message Type | Category | Payload Size | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0x01` | `NewLimitOrder` | Command | 14 bytes | `instrument_id` (4B), `side` (1B), `price` (4B), `qty` (4B), `tif` (1B) |
| `0x02` | `NewMarketOrder` | Command | 9 bytes | `instrument_id` (4B), `side` (1B), `qty` (4B) |
| `0x03` | `CancelOrder` | Command | 12 bytes | `instrument_id` (4B), `order_id` (8B) |
| `0x04` | `ModifyOrder` | Command | 20 bytes | `instrument_id` (4B), `order_id` (8B), `new_price` (4B), `new_qty` (4B) |
| `0x10` | `QueryBook` | Query | 4 bytes | `instrument_id` (4B) |
| `0x11` | `QueryTrades` | Query | 8 bytes | `instrument_id` (4B), `limit` (4B) |
| `0x12` | `QueryOrder` | Query | 8 bytes | `order_id` (8B) |
| `0x80` | `QueryBookResponse` | Response | Variable | `inst_id` (4B), `seq` (8B), `ts` (8B), `b_cnt` (1B), `a_cnt` (1B), levels |
| `0x81` | `QueryTradesResponse`| Response | Variable | `inst_id` (4B), `count` (2B), array of 41-byte trade records |
| `0x82` | `QueryOrderResponse` | Response | 39 bytes | `found` (1B), `order_id` (8B), `inst_id` (4B), `side` (1B), `px`, `qty`, `status`, `ts` |

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

The project contains **89 automated test cases** across C++ and Python suites:

### 1. C++ Engine, Gateway & Read Model Tests (68 tests)
```bash
make test
```
- **`test_engine` (18 tests)**: Core matching semantics (FIFO priority, price priority, GTC/IOC/FOK execution, partial fills, cancellations, multi-instrument isolation).
- **`test_wire` (18 tests)**: Protocol framing, endian-safe serialization/deserialization, frame length validation, boundary conditions, malformed frame detection.
- **`test_gateway` (21 tests)**: Real TCP socket integration via kqueue (server lifecycle, client connections, fragmented TCP frames, 1-byte delivery, multiple clients, backpressure, buffer overflow, TCP queries for book/trades/orders).
- **`test_read_model` (11 tests)**: ReadModel event application, trade history FIFO eviction, bounded order tracking, multi-instrument isolation, concurrent multi-reader safety, and queue drain on shutdown.

### 2. Python REST API Tests (21 tests)
```bash
make test-api
```
- **`test_api` (21 tests)**: HTTP endpoint validation, Pydantic bounds checks, invalid symbols/sides/prices, gateway unreachable handling, health probes, `/book/{symbol}`, `/trades/{symbol}`, `/orders/{order_id}`, and end-to-end HTTP $\to$ FastAPI $\to$ TCP Client $\to$ C++ Gateway $\to$ Matching Engine $\to$ Read Model execution.

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
| Read/Query Plane | In-Memory ReadModel + Projector | DB / Redis / Shared Mutex on Engine | Zero locking/blocking on matching engine hot path |
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
# 1. Run all C++ unit and integration test suites (Engine, Wire, Gateway, Read Model)
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

#### Step 1: Start the C++ TCP Gateway & Read Model
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

#### Step 3: Interact via HTTP REST Endpoints
In terminal 3:
```bash
# 1. Submit Buy Limit order
curl -X POST http://127.0.0.1:8000/orders \
  -H "Content-Type: application/json" \
  -d '{"symbol": "AAPL", "side": "buy", "order_type": "limit", "price": 150, "quantity": 10, "time_in_force": "GTC"}'

# 2. Query L2 Book Depth Snapshot
curl http://127.0.0.1:8000/book/AAPL

# 3. Submit matching Sell Limit order
curl -X POST http://127.0.0.1:8000/orders \
  -H "Content-Type: application/json" \
  -d '{"symbol": "AAPL", "side": "sell", "order_type": "limit", "price": 150, "quantity": 10, "time_in_force": "GTC"}'

# 4. Query Trade History
curl "http://127.0.0.1:8000/trades/AAPL?limit=10"

# 5. Query Order State
curl http://127.0.0.1:8000/orders/1
```

---

## File Structure

```
include/
  types.h               Order, Trade, L2Snapshot structs and type aliases
  price_level.h         PriceLevel: head/tail pointers + total volume
  order_pool.h          Pool allocator with free-list reuse
  orderbook.h           Order book interface
  matching_engine.h     Engine interface with feed and outbound queue wiring
  outbound_events.h     Zero-allocation POD outbound events (Trade, L2Update, OrderState)
  read_model.h          In-memory ReadModel with bounded circular buffer trade history
  projector.h           Dedicated consumer thread projecting outbound events to ReadModel
  network_protocol.h    OrderEvent struct (internal domain format)
  wire_protocol.h       Binary wire format constants, codecs, endian helpers
  tcp_parser.h          Incremental TCP stream parser (Commands & Queries)
  tcp_gateway.h         macOS kqueue gateway and connection management
  spsc_queue.h          Generic lock-free SPSC ring buffer (Command & Outbound queues)
  symbol_registry.h     String symbol to InstrumentId mapping
  market_data.h         L2Snapshot type and MarketDataFeed subscriber
  stats_tracker.h       Per-instrument VWAP, spread, volume tracking

src/
  orderbook.cpp         Price ladder, bitmap, intrusive list, depth query
  matching_engine.cpp   Matching logic, order routing, outbound event emission
  read_model.cpp        ReadModel state management and shared_mutex synchronization
  projector.cpp         Projector event loop and clean drain lifecycle
  tcp_gateway.cpp       kqueue event loop, non-blocking I/O, SPSC handoff, query handler
  gateway_main.cpp      Standalone TCP gateway and ReadModel server entry point

api/
  __init__.py           API module definition
  config.py             Gateway host/port settings & symbol mapping
  models.py             Pydantic models for order commands and read queries
  tcp_client.py         Thread-safe binary TCP client for commands and read queries
  main.py               FastAPI application, command routes, and query routes

scripts/
  client.py             Python binary protocol encoder/decoder and reference client

tests/
  dashboard.cpp         Live ncurses terminal dashboard
  cli.cpp               Interactive CLI — all order types
  simulation.cpp        Multi-threaded producer/consumer, 5M orders
  test_engine.cpp       18-case matching engine correctness suite
  test_wire_protocol.cpp 18-case wire protocol & parser test suite
  test_gateway.cpp      21-case TCP kqueue gateway integration test suite
  test_read_model.cpp   11-case C++ ReadModel and Projector test suite
  test_api.py           21-case FastAPI and end-to-end integration test suite
  benchmark.cpp         Google Benchmark latency suite with naive baseline
```

---

## Notable Fixes During Development

1. **Volume Tracking on Partial Fill**: During stress testing, a partial fill left the order book in an inconsistent state: the matched order's quantity was decremented but the price level's `totalVolume` was not updated. Fixed by ensuring volume is decremented at the price level on every fill.
2. **Double GTC Insertion**: A double insertion bug caused GTC orders to be inserted twice into the order book after partial matches. Fixed by removing the unconditional insert that followed the TIF-gated insert.
3. **Benchmark Instrument Registration**: Fixed an uncaught `Unknown instrument id` runtime exception in benchmark runs by registering `"AAPL"` explicitly before order dispatch.
4. **Signal Safety on Closed Sockets (`SIGPIPE`)**: Writing to disconnected client sockets raised `SIGPIPE` (Exit 141). Resolved by setting `std::signal(SIGPIPE, SIG_IGN)` and configuring `SO_NOSIGPIPE` on macOS sockets.
5. **Socket Port Reuse (`SO_REUSEPORT`)**: Enabled `SO_REUSEPORT` alongside `SO_REUSEADDR` to eliminate port binding collisions during rapid test execution.
6. **Query Trades Frame Stride Alignment**: Fixed a trade record payload calculation in `encode_query_trades_response` where 41-byte struct sizes were packed with a 37-byte offset, restoring exact framing alignment.

---

## What This Is Not

This is an educational electronic exchange simulation, gateway, and REST adapter, not a production trading system. It does not include persistent WAL recovery, FIX protocol compliance, multi-core sharding, or regulatory auditing.

---

## Known Limitations

- **Fixed Price Range (0–100,000)**: The vector price ladder requires a bounded discrete price space.
- **Single-Threaded Matching Core**: The engine itself is not thread-safe; thread boundary isolation is enforced via the SPSC queue.
- **macOS Event Loop**: The gateway uses BSD `kqueue`. A Linux build would require `epoll` or an abstraction layer.
- **In-Memory Only**: No persistent write-ahead logging (WAL) or snapshot replay across process restarts.
