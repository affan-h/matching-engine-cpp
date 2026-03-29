# Limit Order Book Matching Engine (C++)

A production-style C++ implementation of a **price–time priority limit order book and matching engine**, inspired by modern electronic trading systems.

---

## Features

* Limit orders (price-constrained)
* Market orders (liquidity-taking)
* Order cancellation (O(1))
* Order modification (cancel + reinsert)
* FIFO priority within price levels
* Multi-symbol support
* Trade event generation (realistic exchange model)
* Rigorous simulation test suite

---

## Architecture

The system follows a **clean separation of concerns**:

```
Client / Simulation
        │
        ▼
MatchingEngine  (logic)
        │
        ├── symbol → OrderBook
        │
        ▼
OrderBook       (storage)
        │
        ├── bids (map, descending)
        ├── asks (map, ascending)
        └── orderLookup (O(1) cancellation)
        │
        ▼
PriceLevel
        │
        ▼
FIFO list<Order>
```

---

## Core Design Principles

### 1. Separation of Concerns

```
MatchingEngine = logic
OrderBook      = data structure
```

This mirrors real exchange systems.

---

### 2. Price–Time Priority

Matching follows:

1. Better price first
2. FIFO within same price

---

### 3. Trade Execution Rule

```
Trade price = resting order price
```

---

## Core Data Structures

| Structure            | Purpose             |
| -------------------- | ------------------- |
| `std::map`           | Sorted price levels |
| `std::list`          | FIFO order queue    |
| `std::unordered_map` | O(1) order lookup   |
| `Trade struct`       | Execution events    |

---

## Time Complexity

| Operation      | Complexity |
| -------------- | ---------- |
| Insert Order   | O(log P)   |
| Cancel Order   | O(1)       |
| Best Bid / Ask | O(1)       |
| Matching       | O(K)       |

P = number of price levels
K = number of matches

---

## Order Lifecycle

```
Receive order
   ↓
Match against book
   ↓
Generate trade events
   ↓
Insert remaining quantity (if any)
```

---

## Trade Event Model

Each match generates:

```
TRADE <symbol>
  TID=<tradeId>
  TIME=<timestamp>
  BUY=<buyOrderId>
  SELL=<sellOrderId>
  QTY=<quantity>
  PRICE=<price>
```

---

## Simulation Tests

The project includes a structured test suite:

* Basic matching
* Partial fills
* Multi-level matching
* Market order execution
* Cancel validation
* Modify validation
* Stress testing

---

## Key Insights

* Order modification **loses time priority** (real exchange behavior)
* Market orders consume liquidity until exhaustion
* Iterator-based design enables **O(1) cancellation**
* Test isolation is critical to avoid hidden bugs

---

## Challenges Solved

* Fixed incorrect modifyOrder side handling
* Eliminated shared state across tests
* Unified limit and market order processing
* Corrected trade timestamp generation
* Resolved order ID collisions

---

## Future Improvements

* O(1) price ladder (replace `std::map`)
* Cache-friendly data structures
* Event-driven architecture (market data + risk engine)
* Memory pool for ultra-low latency
* Multi-threaded matching pipeline

---

## Current Status

✔ Correct matching engine
✔ Realistic trade event system
✔ Strong architectural design
✔ Interview-ready project

---

## Example Output

```
TRADE AAPL TID=1 TIME=3 BUY=1 SELL=2 QTY=30 PRICE=100
TRADE AAPL TID=2 TIME=5 BUY=7 SELL=6 QTY=30 PRICE=103
```

---

## Learning Outcomes

This project demonstrates:

* System design for trading systems
* Efficient data structure usage
* Event-driven thinking
* Debugging complex stateful systems
* Performance-aware programming in C++

---
