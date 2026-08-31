# Limit Order Book Matching Engine (C++)

A high-performance limit order book matching engine, non-blocking TCP gateway, in-memory read model / projector query plane, and client-facing Python REST API implemented in C++ and Python, modeled after the core infrastructure used in electronic trading systems. Built with a focus on deterministic latency, cache-efficient data structures, lock-free thread boundaries, event-driven network I/O, CQRS command/query plane separation, deterministic client correlation, monotonic event sequencing, and clean API isolation.

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
|  - Request validation: symbols, price ranges, quantities, side, TIF, correlation IDs    |
|  - Thread-safe binary TCP client with automatic reconnect & frame serialization         |
+-----------------------------------------------------------------------------------------+
                                           │
                                           │ Binary TCP Frames (0x01..0x06, 0x10..0x13)
                                           ▼
+-----------------------------------------------------------------------------------------+
|                        C++ TCP GATEWAY (macOS kqueue Event Loop)                        |
|  - Non-blocking listening & client sockets (O_NONBLOCK via fcntl)                       |
|  - Incremental stream parsing (TcpParser) & per-connection state buffers                |
|  - Zero-latency Ping / Pong session heartbeats (0x05 / 0x06)                            |
|  - Direct ReadModel query execution for book depth, trades, orders, and system metrics   |
|  - Bounded backpressure handling and saturation protection                              |
+-----------------------------------------------------------------------------------------+
                                           │
                                           │ OrderEvent Structs (with client_order_id)
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
|  - Monotonic Global Event Sequence Counter (strict causal audit trail)                  |
|  - Price Ladder: direct-indexed vector (O(1) price levels)                              |
|  - Bitmaps: __builtin_clzll / __builtin_ctzll for instantaneous best bid/ask            |
|  - Intrusive doubly linked list & pre-warmed memory pool allocator                      |
|  - Explicit order state lifecycle (New, PartiallyFilled, Filled, Cancelled, Rejected)  |
|  - Deterministic rejection reason codes (InsufficientLiquidityFOK, InvalidPriceQty)    |
|  - Emits fixed-size POD outbound events (Trade, L2Update, OrderState)                   |
+-----------------------------------------------------------------------------------------+
                                           │
                                           │ Fixed-size POD OutboundEvent (with sequence)
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
|  - Client Correlation Index: O(1) client_order_id -> OrderId bidirectional lookup       |
|  - Monotonic State Preservation: terminal states (Filled/Cancelled/Rejected) protected  |
|  - Platform Telemetry: aggregate trades, volume, acceptance, rejections, last sequence  |
|  - Query Synchronization: std::shared_mutex (concurrent multi-reader access)            |
+-----------------------------------------------------------------------------------------+
                                           │
                                           │ std::shared_lock (Cold Query Path)
                                           ▼
+-----------------------------------------------------------------------------------------+
|                 QUERY PLANE REST ENDPOINTS (FastAPI GET /book, /trades, /orders, /stats)|
+-----------------------------------------------------------------------------------------+
```

The system strictly enforces the CQRS (Command Query Responsibility Segregation) pattern:
1. **Execution Hot Path (Zero Mutexes, Zero Allocations)**: `MatchingEngine` produces execution events directly into a lock-free SPSC ring buffer. It never performs socket I/O, never acquires query locks, and never touches FastAPI.
2. **Deterministic Sequencing & Correlation**: Every state change increments a monotonic 64-bit sequence counter. Client correlation IDs (`client_order_id`) are preserved end-to-end from wire frames to engine events to ReadModel indices.
3. **Projection Pipeline**: A dedicated `Projector` background thread consumes from the outbound queue and updates the `ReadModel`.
4. **Query Path (Cold Path Synchronization)**: REST query endpoints read from `ReadModel` using `std::shared_lock<std::shared_mutex>`. Multiple readers query concurrently without blocking each other and without affecting the matching engine.

---

## Core Design Decisions & Reliability Guarantees

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

### Monotonic Event Sequencing & Correlation IDs

Every state change emitted by the engine includes a strictly increasing 64-bit global sequence number (`sequence`) and the original client correlation identifier (`client_order_id`). This provides deterministic execution audit trails and allows clients to query order status directly by their own client correlation ID.

### Explicit Rejection Tracking & Error Reason Codes

The matching engine emits explicit `OrderStatus::Rejected` events with standardized `RejectCode` enums (`InsufficientLiquidityFOK`, `UnknownInstrument`, `InvalidPriceQty`, `OrderNotFound`, `QueueFull`), providing unambiguous feedback for unfillable or invalid orders.

### Monotonic Order State Preservation in ReadModel

The in-memory ReadModel guarantees that terminal order states (`Filled`, `Cancelled`, `Rejected`) cannot regress if older/stale events arrive out of order, ensuring eventual consistency never produces invalid state transitions.

### Graceful Sequential Teardown

Shutdown executes in strict dependency order:
1. `TcpGateway::stop()`: closes listen socket and kqueue, terminates event loop, closes all active client connections, and drains all remaining commands from the SPSC command queue into the engine.
2. `Projector::stop()`: drains all remaining execution and order state events from the outbound SPSC queue into the `ReadModel`, then joins worker threads cleanly without memory leaks or dropped events.

---

## Client-Facing Python REST API Service

The Python API service (`FastAPI` + `Pydantic`) provides a standard HTTP interface while speaking the binary wire protocol to the C++ Gateway over TCP.

### Endpoints

| Method | Path | Plane | Description | Status Code |
| :--- | :--- | :--- | :--- | :--- |
| `POST` | `/orders` | Command | Submit Limit (GTC/IOC/FOK) or Market order with optional `client_order_id` | `202 Accepted` |
| `DELETE` | `/orders/{order_id}` | Command | Cancel resting order by ID (`?symbol=AAPL&client_order_id=...`) | `200 OK` |
| `PATCH` | `/orders/{order_id}` | Command | Modify resting order price and quantity | `200 OK` |
| `GET` | `/book/{symbol}` | Query | Query current L2 order book depth snapshot | `200 OK` / `404` |
| `GET` | `/trades/{symbol}` | Query | Query recent trade execution history (`?limit=50`) | `200 OK` / `404` |
| `GET` | `/orders/{order_id}` | Query | Query order state by Order ID or Client Correlation ID (`?by_client_id=true`) | `200 OK` / `404` |
| `GET` | `/metrics` / `/stats` | Query | Query platform execution statistics and engine telemetry | `200 OK` / `503` |
| `GET` | `/health` | Ops | Active probe checking API & C++ Gateway reachability | `200 OK` / `503` |

### Example REST Requests

#### 1. Submit Limit Buy Order with Client Correlation ID (Command Plane)
```bash
curl -X POST http://127.0.0.1:8000/orders \
  -H "Content-Type: application/json" \
  -d '{"symbol": "AAPL", "side": "buy", "order_type": "limit", "price": 150, "quantity": 10, "time_in_force": "GTC", "client_order_id": 9001}'
```
Response (`202 Accepted`):
```json
{
  "status": "ACCEPTED",
  "symbol": "AAPL",
  "instrument_id": 0,
  "client_order_id": 9001,
  "side": "buy",
  "order_type": "limit",
  "price": 150,
  "quantity": 10,
  "time_in_force": "GTC",
  "message": "Limit order successfully submitted to matching engine gateway"
}
```

#### 2. Query Order Status by Client Correlation ID (Query Plane)
```bash
curl "http://127.0.0.1:8000/orders/9001?by_client_id=true"
```
Response (`200 OK`):
```json
{
  "order_id": 1,
  "client_order_id": 9001,
  "symbol": "AAPL",
  "instrument_id": 0,
  "side": "buy",
  "price": 150,
  "original_quantity": 10,
  "remaining_quantity": 0,
  "filled_quantity": 10,
  "status": "FILLED",
  "reject_code": "NONE",
  "timestamp": 1725012345690,
  "sequence": 4
}
```

#### 3. Query Engine Telemetry Metrics (Query Plane)
```bash
curl http://127.0.0.1:8000/metrics
```
Response (`200 OK`):
```json
{
  "total_trades": 1666666,
  "total_volume": 4166670,
  "total_orders_accepted": 5000000,
  "total_orders_filled": 3333332,
  "total_orders_cancelled": 0,
  "total_orders_rejected": 0,
  "last_sequence": 5000000,
  "tracked_orders_count": 10000,
  "registered_symbols_count": 4,
  "gateway_connected": true
}
```

#### 4. Active Health & Subsystem Readiness Probe
```bash
curl http://127.0.0.1:8000/health
```
Response (`200 OK`):
```json
{
  "status": "healthy",
  "ready": true,
  "gateway": {
    "host": "127.0.0.1",
    "port": 12345,
    "connected": true,
    "rtt_ms": 0.32
  },
  "read_model": {
    "active": true,
    "last_sequence": 5000000,
    "registered_symbols": 4,
    "tracked_orders": 10000,
    "total_trades": 1666666
  },
  "symbols": ["AAPL", "RELIANCE", "INFY", "TATASTEEL"]
}
```

---

## Operational Configuration & Environment Variables

The C++ Gateway and Python REST API support deterministic startup validation and environment variable overrides:

| CLI Option | Environment Variable | Default | Valid Range | Description |
| :--- | :--- | :--- | :--- | :--- |
| `-p`, `--port` | `MATCHING_ENGINE_GATEWAY_PORT` | `12345` | `0..65535` (`0` for ephemeral) | TCP listening port for binary wire protocol |
| `-h`, `--host` | `MATCHING_ENGINE_GATEWAY_HOST` | `127.0.0.1` | Valid IPv4 string | TCP bind address |
| `--command-queue` | `MATCHING_ENGINE_COMMAND_QUEUE_SIZE` | `65536` | `1024..1048576` (Power of 2) | Bounded lock-free SPSC command queue capacity |
| `--outbound-queue` | `MATCHING_ENGINE_OUTBOUND_QUEUE_SIZE`| `65536` | `1024..1048576` (Power of 2) | Bounded lock-free SPSC outbound event queue capacity |
| `--trade-history` | `MATCHING_ENGINE_TRADE_HISTORY_CAP` | `1000` | `10..1000000` | Max recent trade circular buffer capacity per symbol |
| `--order-history` | `MATCHING_ENGINE_ORDER_HISTORY_CAP` | `10000` | `10..1000000` | Max order state record history with FIFO eviction |
| `--max-connections`| — | `1024` | `1..65536` | Maximum concurrent active client connections |
| `-v`, `--verbose` | — | `false` | Boolean flag | Enable structured diagnostic operational logging |

---

## Binary TCP Wire Protocol Specification

The gateway accepts an explicit length-prefixed binary wire format (Big-Endian / Network Byte Order) for Commands, Queries, and Session frames.

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
| `0x00` | `Heartbeat` | Session | 0 bytes | Session liveness tick |
| `0x01` | `NewLimitOrder` | Command | 14B / 22B | Optional `cl_ord_id` (8B), `instrument_id` (4B), `side` (1B), `price` (4B), `qty` (4B), `tif` (1B) |
| `0x02` | `NewMarketOrder` | Command | 9B / 17B | Optional `cl_ord_id` (8B), `instrument_id` (4B), `side` (1B), `qty` (4B) |
| `0x03` | `CancelOrder` | Command | 12B / 20B | Optional `cl_ord_id` (8B), `instrument_id` (4B), `order_id` (8B) |
| `0x04` | `ModifyOrder` | Command | 20B / 28B | Optional `cl_ord_id` (8B), `instrument_id` (4B), `order_id` (8B), `new_price` (4B), `new_qty` (4B) |
| `0x05` | `Ping` | Session | 8 bytes | Client session ping with 8-byte nonce |
| `0x06` | `Pong` | Session | 8 bytes | Gateway pong echoing client nonce |
| `0x10` | `QueryBook` | Query | 4 bytes | `instrument_id` (4B) |
| `0x11` | `QueryTrades` | Query | 8 bytes | `instrument_id` (4B), `limit` (4B) |
| `0x12` | `QueryOrder` | Query | 8B / 9B | `order_id` (8B) or `[1B mode=by_client_id] [8B client_order_id]` |
| `0x13` | `QueryStats` | Query | 0 bytes | Platform execution telemetry request |
| `0x80` | `QueryBookResponse` | Response | Variable | `inst_id` (4B), `seq` (8B), `ts` (8B), `b_cnt` (1B), `a_cnt` (1B), levels |
| `0x81` | `QueryTradesResponse`| Response | Variable | `inst_id` (4B), `count` (2B), array of 41-byte trade records |
| `0x82` | `QueryOrderResponse` | Response | 56 bytes | `found` (1B), `order_id` (8B), `cl_ord_id` (8B), `inst_id` (4B), `side` (1B), `px`, `orig_qty`, `rem_qty`, `filled_qty`, `status` (1B), `reject_code` (1B), `ts` (8B), `seq` (8B) |
| `0x83` | `QueryStatsResponse` | Response | 64 bytes | `trades` (8B), `volume` (8B), `accepted` (8B), `filled` (8B), `cancelled` (8B), `rejected` (8B), `last_seq` (8B), `orders_count` (4B), `symbols_count` (4B) |

---

## Benchmark Results

Measured on macOS, compiled with `g++ -O3`. Compared against a naive baseline using `std::map` + `std::queue`.

| Operation                       | Naive (CPU) | Optimized (CPU) | Notes                     |
|--------------------------------|-------------|-----------------|---------------------------|
| Insert                         | 3962 ns     | 5665 ns         | Price ladder + index setup|
| Match                          | 4075 ns     | 4196 ns         | In-memory fill execution  |
| Cancel (isolated)              | 4141 ns     | 3865 ns         | Direct O(1) index lookup  |
| Cancel (under contention)      | 9023 ns     | 3911 ns         | O(1) index vs O(n) queue  |

**On insert latency:** The optimized engine trades a small constant factor on insert due to indexing initialization across its discrete price ladder and lookup vector.

**On cancel under contention:** The naive implementation scans a queue of N orders to find the target — $O(n)$. The optimized engine uses direct pointer resolution via `orderLookup[id]` — $O(1)$ regardless of queue depth. As shown under contention, optimized cancellation executes in 3.9 µs vs 9.0 µs for naive queue scanning.

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

The project contains **117 automated test cases** across C++ and Python suites:

### 1. C++ Engine, Gateway & Read Model Tests (93 tests)
```bash
make test
```
- **`test_engine` (23 tests)**: Core matching semantics (FIFO priority, price priority, GTC/IOC/FOK execution, partial fills, cancellations, multi-instrument isolation, invalid price/qty rejections, nonexistent cancel rejection reason codes, bit 63 word boundary precision, order modify parameter validation preserving resting orders, and rapid order lifecycle churn).
- **`test_wire` (21 tests)**: Protocol framing, endian-safe serialization/deserialization, frame length validation, boundary conditions, malformed frame detection, Ping/Pong framing, QueryStats decoding, and adversarial fuzzed wire frames.
- **`test_gateway` (32 tests)**: Real TCP socket integration via kqueue (server lifecycle, client connections, fragmented TCP frames, 1-byte delivery, multiple clients, backpressure, buffer overflow, TCP Ping/Pong, TCP queries for book/trades/orders/stats, correlation ID routing, shutdown with active clients, shutdown with partial frames, shutdown command drain, shutdown idempotency, rapid reconnect bursts, deep book queries over TCP, gateway start/stop/restart lifecycle cycles, sustained concurrent client load across 8 threads, and adversarial disconnect bursts during active traffic).
- **`test_read_model` (17 tests)**: ReadModel event application, monotonic global sequencing, FOK rejection reason codes, client correlation index lookup (`getOrderByClientId`), aggregate metrics computation (`getMetrics`), multi-reader thread safety, clean shutdown drain, order state regression prevention, client correlation eviction safety, multi-instrument causal ordering, heavy concurrent readers with parallel writers (8 readers, 2 writers), and projector sustained backpressure with complete shutdown drain.

### 2. Python REST API Tests (24 tests)
```bash
make test-api
```
- **`test_api` (24 tests)**: HTTP endpoint validation, Pydantic bounds checks, invalid symbols/sides/prices, gateway unreachable handling, health probes with Ping/Pong heartbeat verification, `/book/{symbol}`, `/trades/{symbol}`, `/orders/{order_id}`, `/orders/{client_order_id}?by_client_id=true`, `/metrics`, socket reconnect resilience, exhaustive HTTP status code contract (400, 404, 422, 503, 202), and full end-to-end HTTP $\to$ FastAPI $\to$ TCP Client $\to$ C++ Gateway $\to$ Matching Engine $\to$ Read Model execution.

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
# 1. Submit Buy Limit order with client correlation ID
curl -X POST http://127.0.0.1:8000/orders \
  -H "Content-Type: application/json" \
  -d '{"symbol": "AAPL", "side": "buy", "order_type": "limit", "price": 150, "quantity": 10, "time_in_force": "GTC", "client_order_id": 9001}'

# 2. Query L2 Book Depth Snapshot
curl http://127.0.0.1:8000/book/AAPL

# 3. Submit matching Sell Limit order
curl -X POST http://127.0.0.1:8000/orders \
  -H "Content-Type: application/json" \
  -d '{"symbol": "AAPL", "side": "sell", "order_type": "limit", "price": 150, "quantity": 10, "time_in_force": "GTC"}'

# 4. Query Trade History
curl "http://127.0.0.1:8000/trades/AAPL?limit=10"

# 5. Query Order State by Client Correlation ID
curl "http://127.0.0.1:8000/orders/9001?by_client_id=true"

# 6. Query Engine Telemetry Metrics
curl http://127.0.0.1:8000/metrics
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
  read_model.h          In-memory ReadModel with correlation index & telemetry metrics
  projector.h           Dedicated consumer thread projecting outbound events to ReadModel
  network_protocol.h    OrderEvent struct with client_order_id and client_fd
  wire_protocol.h       Binary wire format constants, codecs, endian helpers, Ping/Pong/Stats
  tcp_parser.h          Incremental TCP stream parser (Commands, Queries, Sessions)
  tcp_gateway.h         macOS kqueue gateway and connection management
  spsc_queue.h          Generic lock-free SPSC ring buffer (Command & Outbound queues)
  symbol_registry.h     String symbol to InstrumentId mapping
  market_data.h         L2Snapshot type and MarketDataFeed subscriber
  stats_tracker.h       Per-instrument VWAP, spread, volume tracking

src/
  orderbook.cpp         Price ladder, bitmap, intrusive list, depth query
  matching_engine.cpp   Matching logic, order routing, sequencing, rejection emission
  read_model.cpp        ReadModel state management, correlation lookup, metrics calculation
  projector.cpp         Projector event loop and clean drain lifecycle
  tcp_gateway.cpp       kqueue event loop, non-blocking I/O, SPSC handoff, Ping/Stats dispatch
  gateway_main.cpp      Standalone TCP gateway and ReadModel server entry point

api/
  __init__.py           API module definition
  config.py             Gateway host/port settings & symbol mapping
  models.py             Pydantic models for order commands, queries, and telemetry metrics
  tcp_client.py         Thread-safe binary TCP client for commands, queries, and Ping/Stats
  main.py               FastAPI application, command routes, query routes, and /metrics

scripts/
  client.py             Python binary protocol encoder/decoder and reference client

tests/
  dashboard.cpp         Live ncurses terminal dashboard
  cli.cpp               Interactive CLI — all order types
  simulation.cpp        Multi-threaded producer/consumer, 5M orders
  test_engine.cpp       24-case matching engine correctness suite
  test_wire_protocol.cpp 21-case wire protocol & parser test suite
  test_gateway.cpp      38-case TCP kqueue gateway integration test suite
  test_read_model.cpp   20-case C++ ReadModel and Projector test suite
  test_api.py           26-case FastAPI and end-to-end integration test suite
  benchmark.cpp         Google Benchmark latency suite with naive baseline
```

---

## Durability, Recovery & Crash-Consistency Architecture

### 1. In-Memory Design & Restart Semantics
- **State Lifecycle**: The core matching engine and projection model operate purely in-memory for maximum throughput and sub-microsecond determinism. No disk write-ahead log (WAL) or snapshot persistence is maintained.
- **Process Restart**: Upon startup or process restart, the platform initializes with a clean, deterministic state: sequence numbers begin at 0, order books are empty, and `ReadModel` is marked synchronized with 0 registered trades/orders.
- **Query Determinism**: Freshly started instances immediately serve valid empty responses (e.g. empty depth snapshots with `bid_count=0`, empty trade lists `[]`, and `404 Order Not Found`) without panics or memory corruption.

### 2. End-to-End Graceful Shutdown & Drain Guarantees
- **Ingress Quiescence**: When SIGINT or SIGTERM is received (or `TcpGateway::stop()` is called), the listening socket and `kqueue` loop immediately stop accepting new client connections.
- **Command Queue Drain**: The matching engine consumer thread drains every in-flight command from the `SPSCQueue` into the core engine before exiting.
- **Outbound Projection Drain**: `Projector::stop()` drains all remaining execution events (`Trade`, `OrderState`, `L2Update`) from the `OutboundEventQueue` into the `ReadModel`, ensuring 100% projection consistency prior to process termination.
- **Signal Safety**: Signal handlers execute only lock-free atomic flag stores (`std::atomic<bool> g_shutdown{true}`), remaining strictly POSIX async-signal-safe without memory allocations or mutex locks.

### 3. Command Identity & Client Idempotency
- **Client Correlation IDs**: Every command supports a 64-bit `client_order_id`.
- **Duplicate Prevention & Retry Pattern**: Clients encountering network timeouts or disconnects can query `GET /orders/{client_order_id}?by_client_id=true` to determine if their previous command succeeded before issuing a retry.
- **State Regression Locks**: The `ReadModel` enforces sequence checks and terminal state protection (`Filled`, `Cancelled`, `Rejected` states cannot be regressed by delayed or out-of-order updates).

### 4. Worker Thread Crash Isolation & Health Reflection
- **Exception Boundaries**: Top-level exception handlers in `TcpGateway` and `Projector` catch worker faults, record `worker_fault` atomics, and log diagnostic output without silent thread termination.
- **Health Propagation**: Active component health (`isHealthy()`) is queried by the `/health` endpoint, immediately reporting degraded status (`HTTP 503`) if any background worker encounters a failure.

---

## Notable Fixes During Development

1. **Volume Tracking on Partial Fill**: During stress testing, a partial fill left the order book in an inconsistent state: the matched order's quantity was decremented but the price level's `totalVolume` was not updated. Fixed by ensuring volume is decremented at the price level on every fill.
2. **Double GTC Insertion**: A double insertion bug caused GTC orders to be inserted twice into the order book after partial matches. Fixed by removing the unconditional insert that followed the TIF-gated insert.
3. **Benchmark Instrument Registration**: Fixed an uncaught `Unknown instrument id` runtime exception in benchmark runs by registering `"AAPL"` explicitly before order dispatch.
4. **Signal Safety on Closed Sockets (`SIGPIPE`)**: Writing to disconnected client sockets raised `SIGPIPE` (Exit 141). Resolved by setting `std::signal(SIGPIPE, SIG_IGN)` and configuring `SO_NOSIGPIPE` on macOS sockets.
5. **Socket Port Reuse (`SO_REUSEPORT`)**: Enabled `SO_REUSEPORT` alongside `SO_REUSEADDR` to eliminate port binding collisions during rapid test execution.
6. **Query Trades Frame Stride Alignment**: Fixed a trade record payload calculation in `encode_query_trades_response` where 41-byte struct sizes were packed with a 37-byte offset, restoring exact framing alignment.
7. **Deterministic Sequence Numbers and FOK Rejection Tracking**: Introduced monotonic 64-bit global sequencing across all outbound execution and order state events, coupled with explicit `OrderStatus::Rejected` events with standardized `RejectCode::InsufficientLiquidityFOK` and `RejectCode::OrderNotFound` codes.
8. **Thread-Safe Idempotent Lifecycle & Regression Prevention**: Added atomic compare-and-swap lifecycle transitions for `TcpGateway` and `Projector` along with state-regression locks preventing stale out-of-order updates from regressing terminal order states.
9. **OrderBook Bit 63 Shift Undefined Behavior**: Fixed `findNextBid` where `(1ULL << (bit + 1)) - 1` invoked undefined behavior when `bit == 63`, ensuring word-boundary price levels are never masked to zero.
10. **Order Modify Parameter Validation & Preservation**: Added upfront parameter validation in `modifyOrder` ensuring invalid prices or quantities emit `RejectCode::InvalidPriceQty` and preserve resting orders without corrupting the book, and eliminated duplicate snapshot publications.
11. **Non-Blocking Socket Write Loop**: Replaced raw `send` in TCP gateway query handlers with a bounded `send_all_socket` loop handling `EAGAIN`/`EWOULDBLOCK` and preventing truncated response frames.
12. **ThreadSanitizer Data Race Resolution**: Eliminated asynchronous races in `TcpGateway::stop` vs `runGateway` by converting listening socket and kqueue descriptors into atomic variables and utilizing atomic exchange for shutdown unblocking.
13. **Defensive Non-Zero Capacity Guards**: Added non-zero capacity assertions in `BoundedTradeHistory` and `ReadModel` preventing modulo division-by-zero on edge configurations.
14. **Matching Engine Hot-Path Optimization & Zero-Allocation Depth Extraction**: Streamlined `publishSnapshot` and `cancelOrder` by guarding snapshot generation when no feed subscribers or outbound queues are attached; added `OrderBook::getDepthFast` to eliminate heap vector allocations and linear linked-list node counting during L2 updates; eliminated redundant bitmap lookups in matching loops.
15. **Production Observability, Health & Readiness Semantics, and Config Validation**: Added non-blocking telemetry counters across `MatchingEngine` (accepted/rejected/cancelled counts, traded volume, coalesced drops, critical event retries), `TcpGateway` (connections accepted/closed/rejected, queue drops, overflows), and `Projector` (shutdown drain count); added strict CLI/env configuration bounds validation; enriched `/health` endpoint with active RTT measurements, readiness state, and read model health.
16. **Durability Boundaries, Crash-Consistency & Recovery Hardening**: Hardened worker thread exception boundaries in `TcpGateway` and `Projector`; introduced `worker_fault` and `isHealthy()` lifecycle status; verified empty-state query determinism across `ReadModel`; hardened connection pool recovery and malformed traffic reconnects.

---

## What This Is Not

This is an educational electronic exchange simulation, gateway, and REST adapter, not a production trading system. It does not include persistent WAL recovery, FIX protocol compliance, multi-core sharding, or regulatory auditing.

---

## Known Limitations

- **Fixed Price Range (0–100,000)**: The vector price ladder requires a bounded discrete price space.
- **Single-Threaded Matching Core**: The engine itself is not thread-safe; thread boundary isolation is enforced via the SPSC queue.
- **macOS Event Loop**: The gateway uses BSD `kqueue`. A Linux build would require `epoll` or an abstraction layer.
- **In-Memory Only**: No persistent write-ahead logging (WAL) or snapshot replay across process restarts.
