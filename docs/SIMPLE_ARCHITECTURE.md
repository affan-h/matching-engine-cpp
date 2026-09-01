# Simple Architecture & Mental Model

At a high level, this is a C++ order matching service exposed through a REST API.

---

## The Simple Mental Model

Think of this service like an electronic marketplace for shares (such as Apple or Reliance stock):

1. **A buyer or seller sends an order** via an HTTP API (e.g. "Buy 100 shares of AAPL at $150").
2. **The API accepts the request** immediately and forwards it into the C++ matching engine.
3. **The matching engine checks the order book**:
   - If there is an existing opposite order with a compatible price (e.g. someone is selling at $150 or lower), a **trade occurs immediately**.
   - If no match exists (or for any leftover unfilled shares), the order **rests in the order book** waiting for future matches.
4. **Whenever anything happens** (order placed, trade executed, order cancelled):
   - The engine emits an event.
   - A background projector updates a fast in-memory **Read Model**.
5. **Clients can query the current state** at any time:
   - View market depth (bids and asks).
   - View recent trades.
   - View order status (New, Partially Filled, Filled, Cancelled).
   - View aggregate market statistics.

---

## High-Level Request Flow

```
[ Client / Browser ]
         │
         │ HTTP POST /orders (e.g. "Buy 100 AAPL @ 150")
         ▼
┌──────────────────┐
│  FastAPI Server  │  Validates input, returns HTTP 202 Accepted immediately
└────────┬─────────┘
         │ Binary TCP frame
         ▼
┌──────────────────┐
│   TCP Gateway    │  Receives network bytes, queues into engine
└────────┬─────────┘
         │ In-memory queue
         ▼
┌──────────────────┐
│ Matching Engine  │  Checks order book:
│  (Single Thread) │  - Compatible price found? -> Execute Trade!
└────────┬─────────┘  - No match / remainder?  -> Rest in Order Book!
         │
         │ Execution Event (Trade, State Change, Depth Update)
         ▼
┌──────────────────┐
│ In-Memory Store  │  Maintains up-to-date order book snapshots,
│   (Read Model)   │  recent trade history, and order statuses
└────────▲─────────┘
         │
         │ HTTP GET /book, /trades, /orders
[ Query Clients ]
```

---

## The Order Book in 60 Seconds

The **Order Book** organizes active, resting orders by **Side** and **Price**:

```
                       ASKS (Sellers)
               Price: $152 | Volume: 50
               Price: $151 | Volume: 30
               ------------------------ Spread ($1)
               Price: $150 | Volume: 60
               Price: $149 | Volume: 100
                       BIDS (Buyers)
```

- **Bids**: Buyers wanting to buy at or below their limit price. Highest bid is the "Best Bid".
- **Asks**: Sellers wanting to sell at or above their limit price. Lowest ask is the "Best Ask".
- **Spread**: The gap between Best Ask and Best Bid.
- **Price-Time Priority (FIFO)**:
  1. **Better price wins first** (higher buyers, lower sellers).
  2. **Same price? Older order wins first** (first come, first served).

---

## Why Separate Writing (Matching) from Reading (Queries)?

If querying the order book or fetching recent trades had to pause the matching engine, incoming trade latency would suffer whenever someone refreshed their browser.

By separating the system into two sides:
- **Write Path (Command Plane)**: Fast, single-threaded matching core that only focuses on matching orders and producing execution events.
- **Read Path (Query Plane)**: In-memory read model protected by reader-writer locks so thousands of users can inspect depth and trades simultaneously without blocking trade execution.

---

## Implementation Details Underneath the Model

Once you understand the basic flow above, the lower-level technical choices make natural sense:

1. **Why C++?**
   Predictable sub-microsecond latency, zero garbage collection pauses, and cache-aligned memory control.
2. **Why a single-threaded matching core?**
   Matching is inherently sequential for a single financial instrument. A single thread completely avoids locks, mutex contention, and race conditions on the order book.
3. **How does the Gateway communicate with the Core?**
   Via lock-free Single-Producer Single-Consumer (SPSC) ring buffers, isolating network I/O threads from the matching thread.
4. **How are order books stored?**
   Direct-indexed vector price arrays combined with 64-bit word bitmaps for $O(1)$ hardware-accelerated price traversal, and intrusive doubly linked lists for $O(1)$ order cancellation.
