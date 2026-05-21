# Limit Order Book Matching Engine (C++)

A high-performance matching engine implemented in C++, modeled after  
the core infrastructure used in electronic trading systems. Built with  
a focus on deterministic latency, cache-efficient data structures, and  
real-world exchange design principles.

---

## Demo

```text
===== Matching Engine CLI =====
Type 'help' for commands.

> register AAPL
Registered AAPL as instrument 0

> buy AAPL 100 50

  -- AAPL --
  ASKS:
  ----spread=-1----
       100  qty=    50  orders=1
  mid=0.0

BUY order placed. OrderId=1

> buy AAPL 99 30

  -- AAPL --
  ASKS:
  ----spread=-1----
       100  qty=    50  orders=1
        99  qty=    30  orders=1
  mid=0.0

BUY order placed. OrderId=2

> sell AAPL 101 20

  -- AAPL --
  ASKS:
       101  qty=    20  orders=1
  ----spread=1----
       100  qty=    50  orders=1
        99  qty=    30  orders=1
  mid=100.5

SELL order placed. OrderId=3

> sell AAPL 100 30

  -- AAPL --
  ASKS:
       101  qty=    20  orders=1
  ----spread=1----
       100  qty=    20  orders=1
        99  qty=    30  orders=1
  mid=100.5

SELL order placed. OrderId=4
(trade executed: 30 @ 100 against resting bid)

> ioc sell AAPL 99 100

  -- AAPL --
  ASKS:
       101  qty=    20  orders=1
  ----spread=-1----
  mid=0.0

IOC order placed. OrderId=5
(filled 50 across two price levels, residual 50 discarded)

> fok buy AAPL 101 200
FOK rejected — insufficient liquidity.

> fok buy AAPL 101 20

  -- AAPL --
  ASKS:
  ----spread=-1----
  mid=0.0

FOK filled. OrderId=7

> stats

===== Market Statistics =====

AAPL:
  VWAP         : 99.90
  Total volume : 100
  Book updates : 6
  Avg spread   : 1.00
  High bid     : 100
  Low ask      : 101

=============================

> quit
Goodbye.
```

---

## What It Does

When two traders place opposing orders at compatible prices, a matching  
engine pairs them and generates a trade. This project implements that  
system — the data structures, matching logic, memory management, and  
concurrency layer that make it work at high throughput.

The engine supports multiple instruments simultaneously, a full  
time-in-force order model, a live market data feed with subscriber  
pattern, per-instrument statistics, and an interactive CLI for  
real-time order entry and book inspection.

---

## Architecture

```text
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
        ├──────────────────────────────┐
        ▼                              ▼
   Order Book               Market Data Feed
   (one per instrument)     (L2 snapshots via callbacks)
        │                              │
   ┌────┴────┐                         ├── StatsTracker
   ▼         ▼                         └── CLI display
Price      Order
Levels     Pool             ← zero-allocation hot path
```

The network layer and matching core run on separate threads, connected  
by a lock-free Single-Producer Single-Consumer queue. The matching  
engine itself is entirely single-threaded — no locks, no contention  
on the critical path. The market data feed decouples book state from  
any downstream consumers via a subscriber callback pattern.

---

## Core Design Decisions

### Price Ladder (vector instead of std::map)

The order book uses a direct-indexed array where the index is the  
price. Access is O(1) and cache-friendly. A std::map would give  
O(log n) with heap-allocated nodes scattered in memory.

Tradeoff: fixed price range (0–100,000) and higher upfront memory  
usage. Acceptable for a single-instrument simulation.

---

### Bitmap for Best Bid/Ask

A `uint64_t` bitmap tracks which price levels are active. Finding the  
best bid or ask uses `__builtin_clzll` / `__builtin_ctzll` — single CPU  
instructions. No scanning of empty levels.

---

### Intrusive Linked List

Each price level holds orders in a doubly linked list where next/prev  
pointers live inside the `Order` struct itself. This eliminates the  
separate node allocations that `std::list` requires and keeps orders  
at the same price level contiguous in pool memory.

---

### Memory Pool Allocator

A pre-allocated pool of 2 million `Order` slots replaces `new/delete`  
entirely. A free-list stack handles reuse after cancels. The matching  
loop makes zero heap allocations.

---

### O(1) Cancel via Direct Index

A `vector<Order*>` indexed by `OrderId` gives instant access to any  
resting order. Cancel is a pointer lookup, a linked list unlink, and  
a bitmap clear — no search required.

---

### Market Data Feed

The engine publishes an `L2Snapshot` (top 5 price levels per side) after  
every book change via a subscriber callback. Downstream consumers —  
stats tracker, CLI display — register handlers without the engine  
knowing about them. This mirrors real exchange architectures.

---

## Order Types

| Type       | Behaviour |
|------------|----------|
| Limit GTC  | Rests in book at specified price until filled or cancelled |
| Limit IOC  | Fills available quantity immediately, residual discarded |
| Limit FOK  | Fills entire quantity immediately or cancelled with zero fills |
| Market     | Matches against best available price, no resting |

FOK uses a pre-match volume check — available liquidity at or better  
than the limit price is verified before any trades execute. If  
insufficient, the order is rejected atomically with no side effects.

---

## Benchmark Results

Measured on Azure Standard_F4als_v6 (4 vCores @ 3.69 GHz, 32MB L3  
cache), Ubuntu 24.04, compiled with `g++ -O3`.

| Operation                     | Naive   | Optimized | Notes                     |
|------------------------------|---------|-----------|---------------------------|
| Insert                       | 558 ns  | 992 ns    | See note below            |
| Match                        | 553 ns  | 583 ns    | Similar at low contention |
| Cancel (isolated)            | 582 ns  | 541 ns    | Marginal                  |
| Cancel (1000 resting orders) | 1370 ns | 535 ns    | **2.6x faster**           |

---

## Simulation Results

```text
Orders submitted : 5,000,000
Trades executed  : 1,666,666
Total time       : ~14 seconds (debug machine, thermally throttled)

Per instrument (sample):
  AAPL      VWAP=150.00  volume=4,166,670  book updates=1,250,000
  RELIANCE  VWAP=2800.00 volume=4,166,660  book updates=1,250,000
  INFY      VWAP=1400.00 volume=4,166,660  book updates=1,250,000
  TATASTEEL VWAP=140.00  volume=4,166,670  book updates=1,250,000
```

---

## Test Suite

```text
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

## File Structure

```text
include/
  types.h
  price_level.h
  order_pool.h
  orderbook.h
  matching_engine.h
  network_protocol.h
  spsc_queue.h
  symbol_registry.h
  market_data.h
  stats_tracker.h

src/
  orderbook.cpp
  matching_engine.cpp

tests/
  cli.cpp
  simulation.cpp
  test_engine.cpp
  benchmark.cpp
```

---

## What This Is Not

This is a simulation, not a production system. It does not include  
network I/O, persistence, FIX protocol, or multi-core sharding across  
instruments.

---

## Notable Fix During Development

During stress testing, a partial fill left the order book in an  
inconsistent state: the matched order's quantity was decremented but  
the price level's `totalVolume` was not updated. This caused incorrect  
volume reporting.

Fixed by ensuring volume is decremented at the level on every quantity change.