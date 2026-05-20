# Limit Order Book Matching Engine (C++)

A high-performance matching engine implemented in C++, modeled after
the core infrastructure used in electronic trading systems. Built with
a focus on deterministic latency, cache-efficient data structures, and
real-world exchange design principles.

---

## What It Does

When two traders place opposing orders at compatible prices, a matching
engine pairs them and generates a trade. This project implements that
system — the data structures, matching logic, memory management, and
concurrency layer that make it work at high throughput.

---

## Architecture
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
▼
Order Book               ← one per instrument
│
┌────┴────┐
▼         ▼
Price      Order
Levels     Pool             ← zero-allocation hot path

The network layer and matching core run on separate threads, connected
by a lock-free Single-Producer Single-Consumer queue. The matching
engine itself is entirely single-threaded — no locks, no contention
on the critical path.

---

## Core Design Decisions

### Price Ladder (vector instead of std::map)

The order book uses a direct-indexed array where the index is the
price. Access is O(1) and cache-friendly. A std::map would give
O(log n) with heap-allocated nodes scattered in memory.

Tradeoff: fixed price range (0–100,000) and higher upfront memory
usage. Acceptable for a single-instrument simulation.

### Bitmap for Best Bid/Ask

A uint64_t bitmap tracks which price levels are active. Finding the
best bid or ask uses __builtin_clzll / __builtin_ctzll — single CPU
instructions. No scanning of empty levels.

### Intrusive Linked List

Each price level holds orders in a doubly linked list where next/prev
pointers live inside the Order struct itself. This eliminates the
separate node allocations that std::list requires and keeps orders
at the same price level contiguous in pool memory.

### Memory Pool Allocator

A pre-allocated pool of 2 million Order slots replaces new/delete
entirely. A free-list stack handles reuse after cancels. The matching
loop makes zero heap allocations.

### O(1) Cancel via Direct Index

A vector<Order*> indexed by OrderId gives instant access to any
resting order. Cancel is a pointer lookup, a linked list unlink, and
a bitmap clear — no search required.

---

## Benchmark Results

Measured on Azure Standard_F4als_v6 (4 vCores @ 3.69 GHz, 32MB L3
cache), Ubuntu 24.04, compiled with g++ -O3. Compared against a naive
baseline using std::map + std::queue.

| Operation                     | Naive   | Optimized | Notes                    |
|------------------------------|---------|-----------|--------------------------|
| Insert                       | 558 ns  | 992 ns    | See note below           |
| Match                        | 553 ns  | 583 ns    | Similar at low contention|
| Cancel (isolated)            | 582 ns  | 541 ns    | Marginal                 |
| Cancel (1000 resting orders) | 1370 ns | 535 ns    | **2.6x faster**          |

**On insert latency:** The optimized engine is slower on insert because
its data structures (price ladder + orderLookup vector) are
significantly larger than a std::map, causing cold cache misses on
first access. This is a deliberate tradeoff — real matching engines
experience far more cancel operations than inserts during volatile
markets, so O(1) cancel is the higher-value optimization.

**On cancel under contention:** The naive implementation scans a queue
of N orders to find the target — O(n). The optimized engine uses
orderLookup[id] — O(1) regardless of queue depth. The gap grows
with the number of resting orders at a price level.

---

## Simulation Results

Multi-threaded simulation processing 5 million orders across a
producer thread (network gateway) and consumer thread (matching engine),
connected via the SPSC ring buffer.
Orders submitted : 5,000,000
Trades executed  : 1,666,667
Total time       : ~6.8 seconds

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

---

## Complexity

| Operation    | Complexity | Bottleneck          |
|-------------|-----------|---------------------|
| Insert       | O(1)      | Cache miss on lookup|
| Cancel       | O(1)      | Direct index        |
| Best bid/ask | O(1)      | Bitmap + intrinsic  |
| Match        | O(k)      | k = orders matched  |

---

## Time-In-Force Support

| Type | Behavior |
|------|----------|
| GTC (Good Till Cancel) | Rests in book if unfilled |
| IOC (Immediate Or Cancel) | Fills what it can, discards residual |

---

## How to Build and Run

**Dependencies:** Google Benchmark (for benchmarks only)

**Run simulation:**
```bash
g++ -O2 -std=c++17 \
    src/matching_engine.cpp src/orderbook.cpp tests/simulation.cpp \
    -Iinclude -o sim && ./sim
```

**Run benchmarks:**
```bash
g++ -O3 -std=c++17 \
    tests/benchmark.cpp src/matching_engine.cpp src/orderbook.cpp \
    -Iinclude -lbenchmark -lpthread -o bench && ./bench
```

---

## Notable Bug Fixed During Development

During stress testing, a partial fill left the order book in an
inconsistent state: the matched order's quantity was decremented but
the price level's totalVolume was not updated. This caused the book
to report incorrect available volume at that price. Fixed by ensuring
volume is decremented at the level whenever an order quantity changes.

---

## What This Is Not

This is a simulation, not a production system. It does not include
persistence, network I/O, market data feeds, or multi-instrument
sharding. Those would be the next engineering steps.

---

## File Structure
include/
types.h               Order, Trade structs and type aliases
price_level.h         PriceLevel: head/tail pointers + total volume
order_pool.h          Pool allocator with free-list reuse
orderbook.h           Order book interface
matching_engine.h     Engine interface
network_protocol.h    OrderEvent struct (wire format)
spsc_queue.h          Lock-free SPSC ring buffer
src/
orderbook.cpp         Price ladder, bitmap, intrusive list
matching_engine.cpp   Matching logic, order routing
tests/
simulation.cpp        Multi-threaded producer/consumer simulation
benchmark.cpp         Google Benchmark harness with naive baseline