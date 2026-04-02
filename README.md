# Limit Order Book Matching Engine (C++)

A production-style implementation of a **price–time priority limit order book and matching engine** in C++, inspired by modern electronic trading systems.

---

## 🚀 Features

* Limit orders (Buy / Sell)
* Market orders
* Order cancellation (O(1))
* Order modification (cancel + replace)
* FIFO matching within price levels
* Multi-symbol support
* Trade event generation with timestamps
* Interactive CLI trading interface

---

## 🧠 Architecture

The system is designed with **clear separation of concerns**:

```
Client (CLI)
     │
     ▼
MatchingEngine   ← (logic)
     │
     ▼
OrderBook        ← (data structure)
     │
     ▼
Price Levels (FIFO queues)
```

---

### 🔹 MatchingEngine (Logic Layer)

Responsible for:

* Order lifecycle management
* Matching algorithm (price–time priority)
* Trade generation
* Symbol routing

---

### 🔹 OrderBook (Storage Layer)

Responsible for:

* Maintaining bid/ask ladders
* Managing price levels
* O(1) order lookup for cancellation

---

## ⚙️ Core Design

### Price Ladder (Optimized)

Instead of `std::map`, the order book uses:

```
vector<PriceLevel>
```

* Index = price
* O(1) insertion
* Cache-friendly

---

### Active Price Bitmap

```
bitset<MAX_PRICE>
```

Tracks active price levels:

* Fast best bid/ask lookup
* Avoids scanning empty levels

---

### FIFO at Price Level

```
std::list<Order>
```

* Preserves time priority
* Supports O(1) removal via iterator

---

### O(1) Cancellation

```
unordered_map<OrderId → iterator>
```

* Direct access to orders
* Constant-time deletion

---

## 🔁 Matching Logic

Matching follows **price–time priority**:

```
while (incoming.qty > 0)
    match against best opposing order
```

Rules:

1. Better price executes first
2. Same price → FIFO
3. Partial fills allowed
4. Remaining quantity added to book

---

## 📊 Trade Events

Each trade contains:

* Trade ID
* Symbol
* Buy Order ID
* Sell Order ID
* Quantity
* Price
* Timestamp (real-time)
* Aggressor side (BUY / SELL)

Example:

```
TRADE AAPL TID=1 TIME=2026-03-29 17:45:39.003860 
BUY=100001 SELL=100002 QTY=30 PRICE=100 AGG=BUY
```

---

## 🖥️ CLI Simulator

Interactive trading interface:

```
BUY AAPL 100 50
SELL AAPL 101 20
MARKET_BUY AAPL 30
CANCEL AAPL 100001
MODIFY AAPL 100001 105 40
PRINT AAPL
EXIT
```

---

## ⏱️ Time Complexity

| Operation      | Complexity |
| -------------- | ---------- |
| Insert Order   | O(1)       |
| Cancel Order   | O(1)       |
| Best Bid / Ask | O(1)       |
| Matching       | O(K)       |

K = number of matched orders

---

## 🧪 Testing

Includes:

* Basic matching
* Partial fills
* Multi-level matching
* Market orders
* Cancellation
* Modification
* FIFO validation
* Stress testing

---

## 🧠 Key Learnings

* Designing low-latency data structures
* Managing order lifecycle safely in C++
* Avoiding iterator invalidation bugs
* Building scalable system architecture
* Trade-offs: `map` vs price ladder vs bitmap

---

## 🚀 Future Improvements

* Lock-free matching engine (multi-threaded)
* Memory pool allocator
* Market data feed (order book streaming)
* Persistent storage (replay engine)
* Web-based visualization UI

---

## 📌 Summary

This project demonstrates:

* Strong systems design
* Low-level performance optimization
* Real-world financial system modeling

It is suitable for:

* Backend engineering roles
* Quant / trading system roles
* Low-latency system design discussions

---
# Limit Order Book Matching Engine (C++)

A production-style implementation of a **price–time priority limit order book and matching engine** in C++, inspired by modern electronic trading systems.

---

## 🚀 Features

* Limit orders (Buy / Sell)
* Market orders
* Order cancellation (O(1))
* Order modification (cancel + replace)
* FIFO matching within price levels
* Multi-symbol support
* Trade event generation with timestamps
* Interactive CLI trading interface

---

## 🧠 Architecture

The system is designed with **clear separation of concerns**:

```
Client (CLI)
     │
     ▼
MatchingEngine   ← (logic)
     │
     ▼
OrderBook        ← (data structure)
     │
     ▼
Price Levels (FIFO queues)
```

---

### 🔹 MatchingEngine (Logic Layer)

Responsible for:

* Order lifecycle management
* Matching algorithm (price–time priority)
* Trade generation
* Symbol routing

---

### 🔹 OrderBook (Storage Layer)

Responsible for:

* Maintaining bid/ask ladders
* Managing price levels
* O(1) order lookup for cancellation

---

## ⚙️ Core Design

### Price Ladder (Optimized)

Instead of `std::map`, the order book uses:

```
vector<PriceLevel>
```

* Index = price
* O(1) insertion
* Cache-friendly

---

### Active Price Bitmap

```
bitset<MAX_PRICE>
```

Tracks active price levels:

* Fast best bid/ask lookup
* Avoids scanning empty levels

---

### FIFO at Price Level

```
std::list<Order>
```

* Preserves time priority
* Supports O(1) removal via iterator

---

### O(1) Cancellation

```
unordered_map<OrderId → iterator>
```

* Direct access to orders
* Constant-time deletion

---

## 🔁 Matching Logic

Matching follows **price–time priority**:

```
while (incoming.qty > 0)
    match against best opposing order
```

Rules:

1. Better price executes first
2. Same price → FIFO
3. Partial fills allowed
4. Remaining quantity added to book

---

## 📊 Trade Events

Each trade contains:

* Trade ID
* Symbol
* Buy Order ID
* Sell Order ID
* Quantity
* Price
* Timestamp (real-time)
* Aggressor side (BUY / SELL)

Example:

```
TRADE AAPL TID=1 TIME=2026-03-29 17:45:39.003860 
BUY=100001 SELL=100002 QTY=30 PRICE=100 AGG=BUY
```

---

## 🖥️ CLI Simulator

Interactive trading interface:

```
BUY AAPL 100 50
SELL AAPL 101 20
MARKET_BUY AAPL 30
CANCEL AAPL 100001
MODIFY AAPL 100001 105 40
PRINT AAPL
EXIT
```

---

## ⏱️ Time Complexity

| Operation      | Complexity |
| -------------- | ---------- |
| Insert Order   | O(1)       |
| Cancel Order   | O(1)       |
| Best Bid / Ask | O(1)       |
| Matching       | O(K)       |

K = number of matched orders

---

## 🧪 Testing

Includes:

* Basic matching
* Partial fills
* Multi-level matching
* Market orders
* Cancellation
* Modification
* FIFO validation
* Stress testing

---

## 🧠 Key Learnings

* Designing low-latency data structures
* Managing order lifecycle safely in C++
* Avoiding iterator invalidation bugs
* Building scalable system architecture
* Trade-offs: `map` vs price ladder vs bitmap

---

## 🚀 Future Improvements

* Lock-free matching engine (multi-threaded)
* Memory pool allocator
* Market data feed (order book streaming)
* Persistent storage (replay engine)
* Web-based visualization UI

---

## 📌 Summary

This project demonstrates:

* Strong systems design
* Low-level performance optimization
* Real-world financial system modeling

It is suitable for:

* Backend engineering roles
* Quant / trading system roles
* Low-latency system design discussions

---
