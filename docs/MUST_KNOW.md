# What I Actually Need to Know: 10-Step Interview Study Guide

*You do not need to memorize every line of C++. Follow this 10-step sequence to build a rock-solid, conversational mental model of the project.*

---

## The 10-Day / 10-Step Study Sequence

### Step 1: The Problem
- **What to Learn**: What does an exchange do? Buyers want the lowest price, sellers want the highest price. The service acts as the trusted matching middleman.
- **Key Takeaway**: "It connects buyers and sellers, matches trades at the best price, and stores unmatched orders."

### Step 2: Order Book & Price-Time Priority
- **What to Learn**: How an order book is structured (Bids sorted high-to-low, Asks sorted low-to-high, spread in between).
- **Key Takeaway**: "Better prices match first. If prices are equal, oldest order matches first (FIFO). Trades execute at the resting order's price."

### Step 3: One Complete Order Flow
- **What to Learn**: Client $\rightarrow$ FastAPI $\rightarrow$ TCP Gateway $\rightarrow$ SPSC Queue $\rightarrow$ Matching Engine $\rightarrow$ Order Book $\rightarrow$ Outbound Queue $\rightarrow$ Read Model $\rightarrow$ HTTP Query.
- **Key Takeaway**: "Be able to trace this flow without looking at notes."

### Step 4: Matching Algorithm & Order Types
- **What to Learn**: Limit orders, Market orders, GTC (default), IOC (immediate-or-cancel), and FOK (fill-or-kill).
- **Key Takeaway**: "FOK is atomic: if the full quantity isn't available, 0 shares execute and the book remains untouched."

### Step 5: Core Data Structures
- **What to Learn**: Direct array for price levels (`bids[price]`), 64-bit word bitmaps for hardware bit-scan (`tzcnt`/`lzcnt`), intrusive doubly linked list for orders.
- **Key Takeaway**: "No dynamic heap allocation on the hot path; everything is pre-allocated."

### Step 6: Cancellation & Lifecycle
- **What to Learn**: Why cancellation is strict $O(1)$ (direct pointer array `orderLookup[id]` and intrusive list unlinking).
- **Key Takeaway**: "Conservation invariant: $\text{original\_qty} = \text{filled\_qty} + \text{remaining\_qty} + \text{cancelled\_qty}$."

### Step 7: REST API & Networking
- **What to Learn**: Why `POST /orders` returns 202 Accepted (asynchronous processing), why we use binary packets over TCP (compact, zero-allocation parsing).
- **Key Takeaway**: "Separates fast developer ergonomics (FastAPI) from raw execution speed (C++)."

### Step 8: Concurrency Separation
- **What to Learn**: Why the core is single-threaded (no mutexes/lock contention), why we use an SPSC queue (ring buffer between gateway and engine), why we use CQRS (queries don't lock the matching book).
- **Key Takeaway**: "Matching runs on one thread; queries run on another. They communicate through in-memory queues."

### Step 9: Performance Choices
- **What to Learn**: Why flat arrays beat `std::map` ($O(1)$ vs $O(\log N)$ + cache misses), benchmarks (~5.5 µs insert, ~4.1 µs match, ~3.6 µs cancel).
- **Key Takeaway**: "$O(1)$ cancel is 2.34x faster than naive linked-list search under contention."

### Step 10: Testing & Failure Handling
- **What to Learn**: 142 automated tests, Clang sanitizers (ASan, UBSan, TSan), bounded socket retries (max 500 yields on EAGAIN), graceful shutdown drain linearization.
- **Key Takeaway**: "0 memory leaks, 0 undefined behaviors, 0 data races."

---

## Knowledge Hierarchy Tiers

### Tier 1: MUST KNOW COLD (Zero Hesitation)
1. **30-Second Pitch**: What it does, example of buy/sell match, concurrency separation.
2. **Order Flow**: Client $\rightarrow$ API $\rightarrow$ Gateway $\rightarrow$ Engine $\rightarrow$ Book $\rightarrow$ Read Model.
3. **Price-Time Priority**: Lowest ask matches highest bid; oldest order at same price matches first.
4. **Data Structures**: Direct array price ladder + 64-bit bitmaps + intrusive doubly linked list for $O(1)$ cancel.
5. **Why Single-Threaded Core**: Eliminates locks, mutex contention, and cache bouncing on sequential order books.

---

### Tier 2: SHOULD KNOW (Explain When Asked)
1. **CQRS Architecture**: Command plane (engine) separated from Query plane (Read Model with `std::shared_mutex`).
2. **Lock-Free SPSC Queues**: Single producer, single consumer, acquire/release atomics, `alignas(64)` cache-line padding.
3. **Binary Protocol**: Compact big-endian frames (17 to 25 bytes) parsed in zero allocations.
4. **Client Correlation**: `client_order_id` is an index for queryability before retrying, not automatic duplicate suppression.
5. **Sanitizers**: ASan, UBSan, and TSan (31.5s runtime clean).

---

### Tier 3: NICE TO KNOW (Deep Trivia for Systems Roles)
1. **64-bit Shift Guard**: Shifting `1ULL << 64` when `bit == 63` is undefined behavior; guarded with `(bit == 63) ? ~0ULL : ...`.
2. **TCP Backpressure Bounds**: `send_all_socket` yields up to 500 times (1–5 ms max delay); tears down socket on failure.
3. **Shutdown Linearization**: Stop accepting $\rightarrow$ Drain commands $\rightarrow$ Stop engine $\rightarrow$ Drain events $\rightarrow$ Project events $\rightarrow$ Close connections.
4. **Bit-scan Instructions**: `__builtin_ctzll` compiles to `tzcnt` (x86) or `rbit`+`clz` (ARM); `__builtin_clzll` compiles to `lzcnt`/`clz`.
