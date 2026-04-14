# Limit Order Book Matching Engine (C++)

A production-style implementation of a price–time priority limit order book and matching engine in C++, inspired by modern electronic trading systems and low-latency trading infrastructure.

---

## Overview

This project implements a high-performance matching engine with a strong focus on:

- Deterministic latency
- Cache-efficient data structures
- O(1) operations for critical paths
- Real-world exchange design principles

The system evolved from a basic implementation into a low-latency engine through multiple optimization phases.

---

## Features

- Limit orders (Buy / Sell)
- Market orders
- Order cancellation (O(1))
- Order modification (cancel + replace)
- FIFO matching within price levels
- Multi-symbol support
- Trade event generation with timestamps
- CLI-based simulator
- Stress and correctness testing suite

---

## Architecture
Client (CLI / Simulation)
│
▼
MatchingEngine (Logic Layer)
│
▼
OrderBook (Data Structure Layer)
│
▼
Price Levels (Intrusive FIFO Queues)

---

## Core Design

### Price Ladder

The order book uses a direct-index price ladder:
vector<PriceLevel>

- Index corresponds to price
- Constant-time access
- Cache-friendly layout
- Eliminates tree-based structures

---

### Bitmap for Active Prices

A bitmap tracks active price levels:
vector<uint64_t>

- Fast best bid/ask lookup using bit operations
- Avoids scanning empty levels
- Uses CPU intrinsics for efficiency

---

### Intrusive Linked List

Orders are stored using an intrusive doubly linked list:
Order {
Order* prev;
Order* next;
...
}

- Eliminates std::list overhead
- No iterator indirection
- Improved cache locality

---

### Memory Pool Allocator

Custom memory pool replaces dynamic allocation:

- No malloc/free in hot path
- Deterministic allocation time
- Reduced fragmentation
- Improved performance stability

---

### Direct Index Lookup (Vector-Based)

Replaced hash map with direct indexing:
vector<Order*> orderLookup

- True O(1) lookup
- No hashing or collisions
- Significant improvement in cancel latency

---

## Matching Logic

Matching follows strict price–time priority:
while (incoming.quantity > 0)
     match against best opposing order

Rules:

1. Better price has priority
2. Same price follows FIFO
3. Partial fills supported
4. Remaining quantity is inserted into book

---

## Trade Events

Each trade includes:

- Trade ID
- Symbol
- Buy Order ID
- Sell Order ID
- Quantity
- Price
- Timestamp (microsecond precision)
- Aggressor side

Example:
TRADE AAPL TID=1 TIME=2026-04-14 14:46:17.357856 BUY=100001 SELL=100002 QTY=10 PRICE=100 AGG=SELL

---

## Performance Characteristics

| Operation        | Complexity | Notes                      |
|-----------------|-----------|---------------------------|
| Insert Order     | O(1)      | Memory + cache bound      |
| Cancel Order     | O(1)      | Direct index lookup       |
| Best Bid / Ask   | O(1)      | Bitmap-based              |
| Matching         | O(K)      | K = matched orders        |

---

## Benchmark Results

Measured using a custom benchmarking harness:

| Operation | Latency (approx) |
|----------|------------------|
| Insert   | ~1000 ns         |
| Match    | ~140 ns          |
| Cancel   | ~25 ns           |
| Mixed    | ~1100 ns         |

Key observation:

- System is memory-bound, not CPU-bound
- Matching and cancellation are highly optimized
- Insert latency dominated by cache misses

---

## Testing

The simulation suite covers:

- Basic matching
- Partial fills
- Multi-level matching
- Market orders
- Cancellation
- Modification
- FIFO validation
- Edge cases
- Stress testing

---

## Key Improvements Over Initial Design

| Area | Initial | Final |
|------|--------|-------|
| Price structure | std::map | vector ladder |
| Best price lookup | linear scan | bitmap |
| Order storage | std::list | intrusive list |
| Allocation | new/delete | memory pool |
| Lookup | unordered_map | vector indexing |
| Latency | variable | deterministic |

---

## Notable Bug Fix

Fixed a critical inconsistency in partial fills:

- Previously, order quantity was updated but price level volume was not
- This caused incorrect book state after trades
- Resolved by updating level volume during matching

---

## Design Tradeoffs

### Advantages

- Predictable latency
- High cache efficiency
- Minimal allocation overhead
- Realistic exchange-style architecture

### Tradeoffs

- Fixed price range (vector ladder)
- Higher memory usage (direct indexing)
- Increased implementation complexity
- Manual memory management

---

## Future Work

- Lock-free multi-threaded matching engine
- Per-symbol sharding across CPU cores
- Cache-line alignment and struct packing
- Market data feed (L2/L3 book streaming)
- Persistence and replay system

---

## Summary

This project demonstrates:

- Low-latency systems design
- Data structure optimization for performance
- Cache-aware programming
- Real-world trading system concepts

It is suitable for:

- Backend systems roles
- Quantitative trading roles
- Low-latency and high-performance system design discussions