# Limit Order Book Matching Engine (C++)

A high-performance matching engine implemented in C++, modeled after
the core infrastructure used in electronic trading systems. Built with
a focus on deterministic latency, cache-efficient data structures, and
real-world exchange design principles.

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

When two traders place opposing orders at compatible prices, a matching
engine pairs them and generates a trade. This project implements that
system end-to-end — the data structures, matching logic, memory
management, concurrency layer, market data feed, and a live terminal
dashboard for interacting with the engine in real time.

---

## Architecture

```
Network Gateway (Producer Thread)
        │
        │  OrderEvent structs
        ▼
  SPSC Ring Buffer          ← lock-free handoff between threads
        │
        │  pop()
        ▼
  Matching Engine           ← routes orders by instrument
        │
        ├──────────────────────────────────┐
        ▼                                  ▼
   Order Book                    Market Data Feed
   (one per instrument)          (L2 snapshots via callbacks)
        │                                  │
   ┌────┴────┐                             ├── StatsTracker (VWAP, spread)
   ▼         ▼                             └── Dashboard / CLI display
Price      Order
Levels     Pool             ← zero-allocation hot path
```

The network layer and matching core run on separate threads, connected
by a lock-free SPSC queue. The matching engine is entirely
single-threaded — no locks on the critical path. The market data feed
decouples book state from downstream consumers via subscriber callbacks.

---

## Core Design Decisions

### Price Ladder (vector instead of std::map)

The order book uses a direct-indexed array where the index is the
price. Access is O(1) and cache-friendly. A `std::map` gives O(log n)
with heap-allocated nodes scattered in memory.

Tradeoff: fixed price range (0–100,000) and higher upfront memory
usage. Acceptable for a single-instrument simulation.

### Bitmap for Best Bid/Ask

A `uint64_t` bitmap tracks which price levels are active. Finding the
best bid or ask uses `__builtin_clzll` / `__builtin_ctzll` — single
CPU instructions. No scanning of empty levels.

### Intrusive Linked List

Each price level holds orders in a doubly linked list where `next/prev`
pointers live inside the `Order` struct itself. This eliminates the
separate node allocations that `std::list` requires and keeps orders
at the same price level contiguous in pool memory.

### Memory Pool Allocator

A pre-allocated pool of 2 million `Order` slots replaces `new/delete`
entirely. A free-list stack handles reuse after cancels. The matching
loop makes zero heap allocations.

### O(1) Cancel via Direct Index

A `vector<Order*>` indexed by `OrderId` gives instant access to any
resting order. Cancel is a pointer lookup, a linked list unlink, and
a bitmap clear — no search required.

### Market Data Feed

The engine publishes an L2Snapshot (top 10 price levels per side)
after every book change via a subscriber callback. Downstream
consumers — stats tracker, CLI, dashboard — register handlers without
the engine knowing about them. This mirrors real exchange market data
dissemination architecture.

### Symbol Registry

A bidirectional map between string symbols (`"AAPL"`, `"RELIANCE"`)
and integer `InstrumentId` values sits outside the hot path. The
engine routes entirely by integer ID at runtime — zero string
operations in the matching loop.

---

## Order Types

| Type | Behaviour |
|------|-----------|
| Limit GTC | Rests in book at specified price until filled or cancelled |
| Limit IOC | Fills available quantity immediately, residual discarded |
| Limit FOK | Fills entire quantity or cancelled with zero fills |
| Market    | Matches against best available price, no resting |

FOK uses a pre-match volume check — available liquidity at or better
than the limit price is verified before any trades execute. If
insufficient, the order is rejected atomically with no side effects.

---

## Benchmark Results

Measured on Azure Standard_F4als_v6 (4 vCores @ 3.69 GHz, 32MB L3
cache), Ubuntu 24.04, compiled with g++ -O3. Compared against a naive
baseline using `std::map` + `std::queue`.

| Operation                       | Naive    | Optimized | Notes                     |
|--------------------------------|----------|-----------|---------------------------|
| Insert                         | 558 ns   | 992 ns    | See note below            |
| Match                          | 553 ns   | 583 ns    | Similar at low contention |
| Cancel (isolated)              | 582 ns   | 541 ns    | Marginal                  |
| Cancel (1000 resting orders)   | 1370 ns  | 535 ns    | **2.6x faster**           |

**On insert latency:** The optimized engine is slower on insert because
its data structures (price ladder + orderLookup vector) are
significantly larger than a `std::map`, causing cold cache misses on
first access. This is a deliberate tradeoff — real matching engines
experience far more cancel operations than inserts during volatile
markets, so O(1) cancel is the higher-value optimization.

**On cancel under contention:** The naive implementation scans a queue
of N orders to find the target — O(n). The optimized engine uses
`orderLookup[id]` — O(1) regardless of queue depth. The gap widens
with the number of resting orders at a price level.

---

## Simulation Results

Multi-threaded simulation processing 5 million orders across 4
instruments (AAPL, RELIANCE, INFY, TATASTEEL), distributed via the
SPSC ring buffer across a producer and consumer thread.

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

## Test Suite

18 test cases with zero external dependencies — plain C++ assertions.

```
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
```

---

## Key Design Tradeoffs

| Decision | Chosen | Alternative | Reason |
|---|---|---|---|
| Price structure | vector ladder | std::map | O(1) vs O(log n), cache locality |
| Best price lookup | bitmap + CPU intrinsic | linear scan | Single instruction |
| Order storage | intrusive linked list | std::list | No separate node allocation |
| Memory | pool allocator | new/delete | Zero heap calls in hot path |
| Order lookup | direct index vector | unordered_map | True O(1), no hashing |
| Threading | SPSC queue | mutex + shared queue | Zero contention, no lock overhead |
| Feed delivery | callback subscribers | polling | Decoupled, extensible |
| Symbol routing | integer InstrumentId | string map | No string ops in hot path |

---

## Complexity

| Operation    | Complexity | Bottleneck             |
|-------------|-----------|------------------------|
| Insert       | O(1)      | Cache miss on lookup   |
| Cancel       | O(1)      | Direct index           |
| Best bid/ask | O(1)      | Bitmap + intrinsic     |
| Match        | O(k)      | k = orders matched     |
| FOK check    | O(L)      | L = price levels scanned |
| Depth query  | O(N)      | N = levels returned    |

---

## How to Build and Run

**Dependencies:** Google Benchmark (benchmarks only), ncurses (dashboard only), C++17 compiler

**Terminal dashboard (recommended — most interactive):**
```bash
make dashboard
```
Controls: `[UP][DN]` switch instrument — `,` scroll resting down — `.` scroll resting up — `q` quit

**Dashboard commands:**
```
buy  <SYMBOL> <PRICE> <QTY>          GTC limit buy
sell <SYMBOL> <PRICE> <QTY>          GTC limit sell
ioc  buy|sell <SYMBOL> <PRICE> <QTY> Immediate-Or-Cancel
fok  buy|sell <SYMBOL> <PRICE> <QTY> Fill-Or-Kill
market buy|sell <SYMBOL> <QTY>       Market order
cancel <SYMBOL> <ORDER_ID>           Cancel resting order
?                                    Show help
q                                    Quit
```

**Interactive CLI:**
```bash
make cli
```

**Multi-instrument simulation:**
```bash
make sim
```

**Test suite:**
```bash
make test
```

**Benchmarks (requires Google Benchmark):**
```bash
make bench
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
  network_protocol.h    OrderEvent struct (wire format)
  spsc_queue.h          Lock-free SPSC ring buffer
  symbol_registry.h     String symbol to InstrumentId mapping
  market_data.h         L2Snapshot type and MarketDataFeed subscriber
  stats_tracker.h       Per-instrument VWAP, spread, volume tracking

src/
  orderbook.cpp         Price ladder, bitmap, intrusive list, depth query
  matching_engine.cpp   Matching logic, order routing, feed publishing

tests/
  dashboard.cpp         Live ncurses terminal dashboard
  cli.cpp               Interactive CLI — all order types
  simulation.cpp        Multi-threaded producer/consumer, 5M orders
  test_engine.cpp       18-case correctness test suite
  benchmark.cpp         Google Benchmark harness with naive baseline
```

---

## Notable Fix During Development

During stress testing, a partial fill left the order book in an
inconsistent state: the matched order's quantity was decremented but
the price level's `totalVolume` was not updated. This caused the book
to report incorrect available volume at that price level. Fixed by
ensuring volume is decremented at the level on every quantity change.

A second bug — double insertion — caused GTC orders to be inserted
twice into the order book after partial matches. Fixed by removing the
unconditional insert that followed the TIF-gated insert.

---

## What This Is Not

This is a simulation, not a production system. It does not include
network I/O, persistence, FIX protocol, or multi-core sharding. Those
would be the next engineering steps.

---

## Known Limitations

- Fixed price range (0–100,000): the vector ladder requires a bounded,
  discrete price space. Real instruments with arbitrary prices would
  require a different indexing strategy.
- Single-threaded matching core: the engine itself is not thread-safe.
  Concurrency is handled at the boundary via the SPSC queue.
- In-memory only: no persistence or replay. Restarting loses all state.