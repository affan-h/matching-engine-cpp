# Matching Engine

An in-memory electronic limit order matching service written in modern C++ (C++17) with an event-driven network gateway and a client-facing Python FastAPI REST interface.

---

## What is this?

This project is an in-memory electronic order matching service written in modern C++ (C++17) with a Python FastAPI REST interface. Clients can submit buy and sell orders through standard HTTP endpoints. The matching engine maintains an order book and matches compatible orders using fair price-time priority.

For example, if someone places a buy order for 100 shares of Apple at $150 and another client sells 40 shares at $150, the engine executes a 40-share trade immediately and leaves the remaining 60 shares resting in the book.

Networking and read queries are decoupled from the core matching engine so that high-volume market queries never interfere with trade execution.

---

## What can it do?

- **Match Orders Fairly**: Implements standard Price-Time Priority (FIFO) across multiple instruments (AAPL, RELIANCE, INFY, TATASTEEL).
- **Multiple Order Types**: Supports Limit Orders, Market Orders, and Time-in-Force policies (Good-Til-Cancelled / GTC, Immediate-Or-Cancel / IOC, Fill-Or-Kill / FOK).
- **Fast O(1) Cancellation**: Unlinks resting orders from the book in constant time using intrusive pointers and pre-allocated memory.
- **RESTful HTTP Interface**: Submit orders and query live book depth and trade history via standard JSON endpoints (`/orders`, `/book/{symbol}`, `/trades/{symbol}`, `/health`).
- **High-Throughput Concurrency**: Decouples networking from the core matching engine using lock-free Single-Producer Single-Consumer (SPSC) ring buffers.
- **Battle-Tested Reliability**: 142 automated tests verified clean under Clang AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer.

---

## How it works

The system uses a **Command Query Responsibility Segregation (CQRS)** pattern to keep order matching completely isolated from client read queries:

```
[ HTTP REST Clients ]             [ High-Frequency TCP Clients ]
         │                                      │
         │ HTTP JSON                            │ Binary Wire Frames
         ▼                                      ▼
┌──────────────────┐                  ┌──────────────────┐
│  FastAPI Server  │                  │   TCP Gateway    │
│  (Python Async)  │                  │  (kqueue Reactor)│
└────────┬─────────┘                  └────────┬─────────┘
         │                                     │
         └─────────────► [ Binary TCP ] ───────┘
                               │
                               ▼
               ┌────────────────────────────────┐
               │ Lock-Free SPSC Command Queue   │ (64-byte cacheline isolation)
               └───────────────┬────────────────┘
                               │
                               ▼
               ┌────────────────────────────────┐
               │     Matching Engine Core       │ - Single-threaded lock-free core
               │       (C++ OrderBook)          │ - Zero heap allocations on hot path
               └───────────────┬────────────────┘ - Hardware bit-scan (tzcnt/lzcnt)
                               │
                               ▼
               ┌────────────────────────────────┐
               │ Lock-Free SPSC Outbound Queue  │ (Monotonic 64-bit sequence)
               └───────────────┬────────────────┘
                               │
                               ▼
               ┌────────────────────────────────┐
               │        Projector Worker        │
               └───────────────┬────────────────┘
                               │ (Exclusive Write Lock)
                               ▼
               ┌────────────────────────────────┐
               │      In-Memory Read Model      │ - Thread-safe (std::shared_mutex)
               │    (Depth, Trades, Orders)     │ - Concurrent shared reads for API
               └────────────────────────────────┘
```

1. **Write Path (Orders)**: Clients send requests to FastAPI, which serializes them into compact big-endian binary frames and sends them over TCP to the C++ Gateway.
2. **Ingress Reactor**: The Gateway uses non-blocking BSD `kqueue` sockets to receive frames and pushes them into a lock-free SPSC command queue.
3. **Matching Core**: A dedicated matching thread pops commands and matches orders sequentially without taking any locks or making heap allocations.
4. **Read Path (Queries)**: Execution events flow through an outbound queue to a background Projector worker, updating an in-memory Read Model. REST clients query the Read Model with shared read locks (`std::shared_lock`), never blocking the matching core.

---

## Example Walkthrough

### 1. Submit a Resting BUY Order
A buyer submits a limit order for 100 shares of Apple at \$150:
```bash
curl -X POST http://127.0.0.1:8000/orders \
  -H "Content-Type: application/json" \
  -d '{"symbol":"AAPL","side":"buy","order_type":"limit","price":150,"quantity":100}'
```
*Response*: `{"status":"ACCEPTED","order_type":"limit","price":150,"quantity":100}`

### 2. Inspect the Order Book
```bash
curl -X GET http://127.0.0.1:8000/book/AAPL
```
*Output*:
```json
{
  "symbol": "AAPL",
  "bids": [{"price": 150, "quantity": 100}],
  "asks": []
}
```

### 3. Submit an Aggressive SELL Order (Partial Match)
A seller submits 40 shares at \$150:
```bash
curl -X POST http://127.0.0.1:8000/orders \
  -H "Content-Type: application/json" \
  -d '{"symbol":"AAPL","side":"sell","order_type":"limit","price":150,"quantity":40}'
```

### 4. Query the Trade Log
A trade for 40 shares executes immediately at \$150:
```bash
curl -X GET http://127.0.0.1:8000/trades/AAPL
```
*Output*:
```json
{
  "symbol": "AAPL",
  "trades": [
    {
      "trade_id": 1,
      "price": 150,
      "quantity": 40,
      "aggressor_side": "sell"
    }
  ]
}
```

### 5. Verify the Updated Book
The resting buy order has automatically decremented from 100 to 60 shares:
```bash
curl -X GET http://127.0.0.1:8000/book/AAPL
```
*Output*:
```json
{
  "symbol": "AAPL",
  "bids": [{"price": 150, "quantity": 60}],
  "asks": []
}
```

---

## Order Lifecycle

Every order obeys the conservation invariant:
$$\text{original\_qty} = \text{filled\_qty} + \text{remaining\_qty} + \text{cancelled\_qty}$$

```
                ┌──────────────┐
                │  New Order   │
                └──────┬───────┘
                       │
          ┌────────────┴────────────┐
          ▼                         ▼
   [ Instant Match ]         [ No Match (Resting) ]
          │                         │
     ┌────┴────┐                    │
     ▼         ▼                    ▼
 (Full Fill) (Partial Fill) ──► (Partially Filled Resting)
     │         │                    │
     │         ▼                    ├─────────────────┐
     │      (Filled)                ▼                 ▼
     │         │             (Subsequent Match)  (User Cancel)
     ▼         ▼                    │                 │
 ┌─────────────────┐                ▼                 ▼
 │     FILLED      │             (Filled)        ┌───────────┐
 └─────────────────┘                             │ CANCELLED │
                                                 └───────────┘
```

- **NEW**: Order is validated and accepted into the system.
- **PARTIALLY_FILLED**: Part of the requested quantity executed; the remainder rests on the book.
- **FILLED**: 100% of the requested quantity executed (`remaining_qty == 0`). Terminal state.
- **CANCELLED**: The unfilled remainder was revoked by the user. Terminal state.
- **REJECTED**: Order could not be executed (e.g. invalid price, or FOK order with insufficient liquidity). Terminal state.

---

## Important Design Decisions

| Decision | Why It Was Chosen |
| :--- | :--- |
| **Why C++?** | Sub-microsecond deterministic execution, explicit memory alignment, zero garbage collection pauses, and access to hardware bit-scan instructions. |
| **Why single-threaded matching core?** | Order matching for an instrument is inherently sequential. A single thread completely avoids locks, mutex contention, and CPU cache invalidation loops. |
| **Why price-time priority?** | Industry standard FIFO execution: better prices match first; identical prices match oldest order first. |
| **Why direct vector arrays + bitmaps?** | Instead of a slow $O(\log N)$ tree like `std::map`, a flat array provides $O(1)$ price lookup, and 64-bit bitmaps find the best bid/ask in 1 CPU instruction (`lzcnt`/`tzcnt`). |
| **Why intrusive doubly linked lists?** | Embedding `prev` and `next` pointers inside pre-allocated `Order` structs allows $O(1)$ order cancellation without scanning the list or allocating heap nodes. |
| **Why lock-free SPSC ring buffers?** | Isolates networking threads from the matching core with wait-free atomic operations, preventing thread preemption and false sharing. |
| **Why an in-memory Read Model?** | Separates read traffic from matching. Thousands of dashboard users can query market depth simultaneously without locking the order book. |

---

## Running Locally

### Prerequisites
- macOS (uses BSD `kqueue`)
- Clang / GCC supporting C++17
- Python 3.10+ with `venv`

### Setup
```bash
# 1. Create and activate virtual environment
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

# 2. Build the C++ Gateway
make gateway

# 3. Start the C++ Gateway (Terminal 1)
./gateway -p 12345

# 4. Start the FastAPI REST API (Terminal 2)
make api
```
Access the interactive OpenAPI documentation at **`http://127.0.0.1:8000/docs`**.

---

## 2-Minute Demo

Run the automated, self-contained end-to-end demo script:
```bash
./scripts/demo.sh
```
This script dynamically finds free ports, starts both services, submits orders, executes a trade, cancels a resting order, tests a Fill-Or-Kill rejection, and cleanly shuts down background processes.

For a spoken interview script detailing what to say step-by-step, see **[`docs/DEMO_GUIDE.md`](docs/DEMO_GUIDE.md)**.

---

## Automated Test Suites

The repository contains **142 automated tests** across C++ and Python:

| Suite | Component | Count | Verification |
| :--- | :--- | :--- | :--- |
| `test_engine.cpp` | Matching Engine Core & OrderBook | 26 | Price priority, FOK/IOC, churn, volume conservation |
| `test_wire_protocol.cpp` | Binary Wire Protocol & Parser | 23 | Framed packets, fuzzing, fragmentation, SPSC bounds |
| `test_read_model.cpp` | ReadModel & Projector Worker | 24 | Sequence ordering, terminal lock, multi-instrument |
| `test_gateway.cpp` | TCP kqueue Gateway | 42 | Socket churn, multi-client, backpressure, restart |
| `test_api.py` | FastAPI HTTP REST Layer | 27 | Schema validation, health, end-to-end integration |
| **Total Tests** | | **142** | **142 / 142 PASS** |

### Running Tests
```bash
make test       # Runs all 115 C++ unit and integration tests
make test-api   # Runs all 27 Python pytest integration tests
```

### Sanitizer Verification
- **AddressSanitizer / UndefinedBehaviorSanitizer**: 115/115 tests clean (0 memory leaks, 0 buffer overflows, 0 undefined behaviors).
- **ThreadSanitizer (TSan)**: 66/66 multithreaded tests clean in **31.54s** (0 data races, 0 deadlocks).

---

## Performance Benchmarks

Measured using Google Benchmark on macOS (Release build `-O3`):

| Benchmark Operation | Measured CPU Time | Complexity | What It Means |
| :--- | :--- | :--- | :--- |
| **`BM_InsertOrder`** | **5.48 µs** | $O(1)$ | Direct vector lookup, intrusive node insertion, and bitmap update. |
| **`BM_MatchOrder`** | **4.09 µs** | $O(1)$ | Instant hardware bit-scan to find best price level and execute trade. |
| **`BM_CancelOrder`** | **3.64 µs** | $O(1)$ | Instant pointer unlink from doubly linked list without book search. |
| **`BM_CancelOrder_Contention`**| **3.54 µs** | $O(1)$ | **2.34x faster** than naive scan (8.30 µs) under high book depth. |

### 5-Million Order Simulation
```bash
make sim
```
Simulates 5,000,000 orders across 4 instruments, generating 1,666,666 trades with 100% volume and VWAP conservation under multi-threaded producer/consumer load.

---

## Trade-offs and Limitations

- **Discrete Price Space (1..100,000)**: The direct-indexed price vector ladder requires a bounded discrete integer price space. Extreme or fractional price ranges would require price compression or multi-tier ladders.
- **Single-Threaded Matching Core**: One matching thread per instrument. Multi-core scaling is achieved by partitioning across financial instruments, not by multi-threading a single order book.
- **macOS Event Loop**: The gateway uses BSD `kqueue`. Running on Linux requires implementing an `epoll` reactor adapter.
- **In-Memory Volatility**: Designed as an in-memory matching simulation. It does not include persistent disk write-ahead logging (WAL) or recovery replay across process restarts.

---

## Future Improvements

1. **Persistent Write-Ahead Log (WAL)**: Append-only disk logging using memory-mapped files (`mmap`) for crash recovery.
2. **Linux `epoll` Demultiplexer**: Platform abstraction layer enabling native deployment on Linux servers.
3. **Multi-Instrument Sharding**: Dynamically spawn dedicated worker threads per instrument to scale across multi-socket CPUs.
4. **WebSocket Level 2 Market Feed**: Stream live depth deltas and trade prints directly to web clients.

---

## Interview Documentation

- **[`docs/SIMPLE_ARCHITECTURE.md`](docs/SIMPLE_ARCHITECTURE.md)**: High-level intuitive mental model.
- **[`docs/DEMO_GUIDE.md`](docs/DEMO_GUIDE.md)**: Step-by-step 2-minute live demo script with spoken talking points.
- **[`docs/INTERVIEW_GUIDE.md`](docs/INTERVIEW_GUIDE.md)**: 30s / 90s pitches and deep-dive technical explanations.
- **[`docs/DESIGN_DECISIONS.md`](docs/DESIGN_DECISIONS.md)**: "Why did you do this?" trade-off comparison matrix.
- **[`docs/INTERVIEW_QUESTIONS.md`](docs/INTERVIEW_QUESTIONS.md)**: Question bank organized into 11 categories.
- **[`docs/RESUME_PROJECT_DESCRIPTION.md`](docs/RESUME_PROJECT_DESCRIPTION.md)**: Tailored resume bullets.
- **[`docs/MUST_KNOW.md`](docs/MUST_KNOW.md)**: Tier 1, 2, and 3 interview knowledge hierarchy.
