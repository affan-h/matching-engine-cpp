# Master Interview Question Bank: Progressive 3-Level Breakdown

*Organized from conversational fundamentals (Level 1), to architectural decisions (Level 2), to deep systems and concurrency mechanics (Level 3).*

---

## Level 1: Core Fundamentals & Workflow

### 1. "What does your project do?"
- **What Interviewer is Testing**: Clarity of thought, communication, and high-level comprehension.
- **Strong Answer**:
  > "I built an in-memory order matching service in C++. Clients can submit buy and sell orders through a REST API. The matching engine maintains an order book and matches compatible orders using price-time priority. For example, if someone places a buy order for 100 shares at $150 and another client sells 40 shares at $150, the engine executes a 40-share trade and leaves 60 shares resting in the book. I separated networking and read queries from the core matching logic so the matching path stays simple and deterministic."
- **Possible Follow-up**: *"Who are the clients?"*
- **Follow-up Answer**:
  > "Clients can be web dashboards querying live market depth, or algorithmic trading scripts submitting limit and market orders via HTTP or raw TCP sockets."

---

### 2. "Can you walk me through the lifecycle of an order?"
- **What Interviewer is Testing**: End-to-end trace ability through the code.
- **Strong Answer**:
  > "An order starts at the REST API via `POST /orders`. FastAPI validates the request and forwards a 17-byte binary frame over TCP to our C++ Gateway. The gateway queues it into an in-memory ring buffer. The matching engine pops the order and checks the order book. If there is an opposite order with a matching price, a trade occurs immediately. If not, or if shares remain, the order rests in the book. The engine emits an event to an outbound queue, where a background worker updates an in-memory Read Model that clients can query."
- **Possible Follow-up**: *"What states can an order have?"*
- **Follow-up Answer**:
  > "`NEW` when accepted, `PARTIALLY_FILLED` if part matches and part rests, `FILLED` when 100% executes, `CANCELLED` if revoked by the user, and `REJECTED` if invalid or if a Fill-or-Kill order cannot be satisfied."

---

### 3. "How does matching work?"
- **What Interviewer is Testing**: Understanding of Price-Time Priority (FIFO).
- **Strong Answer**:
  > "It uses Price-Time Priority. Buy orders match against the lowest ask; sell orders match against the highest bid. If multiple resting orders exist at the same price, the order that was placed first is matched first. Crucially, the trade executes at the resting order's price, giving the incoming order price improvement."
- **Possible Follow-up**: *"What if the incoming order has a price between the best bid and best ask?"*
- **Follow-up Answer**:
  > "No match occurs. The order rests on the book between the spread, narrowing the market spread for future participants."

---

### 4. "What is an order book?"
- **What Interviewer is Testing**: Basic domain knowledge of financial markets.
- **Strong Answer**:
  > "An order book is a structured ledger of resting buy and sell orders organized by price. Bids are buyers sorted from highest price to lowest price; asks are sellers sorted from lowest price to highest price. The gap between the highest bid and lowest ask is the spread."
- **Possible Follow-up**: *"What is Level 2 market depth?"*
- **Follow-up Answer**:
  > "Level 2 depth aggregates individual orders into total volume at each price level, showing buyers and sellers how much liquidity is available at different prices."

---

### 5. "How do you handle partial fills?"
- **What Interviewer is Testing**: State consistency and quantity tracking.
- **Strong Answer**:
  > "When an aggressive order matches a resting order that has fewer shares than requested, the resting order is filled completely, its quantity trades, and it is removed from the book. The aggressive order's remaining quantity is updated, and it continues matching down the book. If liquidity runs out, the unfilled remainder rests as a new active order on the book."
- **Possible Follow-up**: *"What invariant ensures quantity isn't lost?"*
- **Follow-up Answer**:
  > "The conservation equation: $\text{original\_qty} = \text{filled\_qty} + \text{remaining\_qty} + \text{cancelled\_qty}$ must hold across every single order transition."

---

### 6. "How does cancellation work?"
- **What Interviewer is Testing**: Data structure retrieval and removal mechanics.
- **Strong Answer**:
  > "When a cancel request arrives with an order ID, the engine looks up the order in a direct pointer table in $O(1)$ time, unlinks it from its price level's doubly linked list in $O(1)$ time, decrements the price level's total volume, and returns the order node to the pool's free-list."
- **Possible Follow-up**: *"What happens if the cancelled order was the only order at that price?"*
- **Follow-up Answer**:
  > "The price level's volume becomes zero, and the engine clears the corresponding bit in the 64-bit word bitmap so subsequent matching operations skip that price level."

---

## Level 2: Design Decisions & Data Structures

### 7. "Why did you write the core in C++?"
- **What Interviewer is Testing**: Language trade-offs and performance rationale.
- **Strong Answer**:
  > "Because order matching requires deterministic, sub-microsecond latency and explicit control over memory layout. Languages like Java or Go have garbage collection pauses that introduce unpredictable latency spikes at the 99th percentile. C++ provides manual memory management, zero-cost abstractions, and direct access to CPU hardware bit-scan instructions."
- **Possible Follow-up**: *"Why not write the REST API in C++ too?"*
- **Follow-up Answer**:
  > "Python with FastAPI gives us fast development, automated OpenAPI documentation, and rich JSON validation. Keeping the API in Python and the matching core in C++ gives us the best of both worlds: developer ergonomics on the outside and pure execution performance on the inside."

---

### 8. "Why did you choose your specific order book data structures?"
- **What Interviewer is Testing**: Algorithm and data structure trade-offs.
- **Strong Answer**:
  > "Standard implementations use `std::map` (a red-black tree), which takes $O(\log N)$ time and causes CPU cache misses by jumping through scattered pointers. Instead, I used a direct-indexed vector price ladder combined with 64-bit word bitmaps. Finding the best bid or ask takes a single CPU instruction (`tzcnt`/`lzcnt`), and accessing any price level is a direct array offset ($O(1)$)."
- **Possible Follow-up**: *"What are the trade-offs of this approach?"*
- **Follow-up Answer**:
  > "It requires prices to be bounded discrete integers (in our case 1 to 100,000). For standard equities with discrete tick sizes this works well, but arbitrary floating-point prices would require price scaling or a hybrid data structure."

---

### 9. "Why is cancellation strict O(1)?"
- **What Interviewer is Testing**: Intrusive data structures and pointer mechanics.
- **Strong Answer**:
  > "Orders are stored in an intrusive doubly linked list, meaning the `prev` and `next` pointers live directly inside the `Order` struct itself. When cancelling, we look up `orderLookup[order_id]` in $O(1)$, and unlink `node->prev` and `node->next` in $O(1)$ without scanning the list. In benchmarks under heavy book contention, this was 2.34x faster than naive list traversal (3.5 µs vs 8.3 µs)."
- **Possible Follow-up**: *"Why not use `std::list`?"*
- **Follow-up Answer**:
  > "`std::list` allocates an extra heap wrapper node for each order, causing heap allocation overhead and poor cache locality. Intrusive lists keep the node memory contiguous."

---

### 10. "Why is the matching core single-threaded?"
- **What Interviewer is Testing**: Concurrency paradigms and lock contention.
- **Strong Answer**:
  > "Matching orders for a single stock is fundamentally sequential: Order B's execution depends on whether Order A traded first. If multiple threads try to mutate the same order book, you need mutexes. Under high contention, threads spend all their time waiting on locks and bouncing cache lines. A dedicated single thread has zero lock overhead, zero context switching, and keeps book data hot in L1/L2 cache."
- **Possible Follow-up**: *"How do you scale to handle multiple stocks?"*
- **Follow-up Answer**:
  > "By sharding across financial instruments: Thread 1 matches Apple, Thread 2 matches Reliance. Each instrument has its own independent single-threaded matching core."

---

### 11. "How do network requests reach the engine?"
- **What Interviewer is Testing**: Network I/O and protocol mechanics.
- **Strong Answer**:
  > "FastAPI receives HTTP requests, validates the schema with Pydantic, and serializes the order into a compact big-endian binary frame (17 bytes for limit orders). It sends this over TCP to our C++ Gateway, which parses the frame and pushes it into an SPSC command ring buffer."
- **Possible Follow-up**: *"Why a binary protocol instead of sending JSON directly to the C++ core?"*
- **Follow-up Answer**:
  > "Binary packets are tiny, require zero heap allocation to parse, and avoid expensive string-parsing and JSON-decoding libraries in the C++ hot path."

---

### 12. "How do query endpoints work without locking the matching engine?"
- **What Interviewer is Testing**: CQRS architecture and read/write decoupling.
- **Strong Answer**:
  > "We use a Command Query Responsibility Segregation (CQRS) model. When trades happen, the matching engine writes execution events to an outbound queue. A background Projector worker reads these events and updates an in-memory Read Model. REST queries for depth or trade history read from this Read Model using shared reader locks (`std::shared_lock`), completely isolated from the matching thread."
- **Possible Follow-up**: *"What if the reader queries while the projector is updating?"*
- **Follow-up Answer**:
  > "The Read Model is guarded by `std::shared_mutex`. Readers acquire shared locks simultaneously, while the projector acquires an exclusive write lock (`std::unique_lock`) only for the microsecond it takes to apply an event."

---

## Level 3: Deep Systems & Concurrency Mechanics

### 13. "Why use an SPSC queue and how does it work?"
- **What Interviewer is Testing**: Lock-free ring buffer design and memory ordering.
- **Strong Answer**:
  > "Because the network gateway acts as a single producer and the matching engine acts as a single consumer, an SPSC queue provides wait-free, lock-free communication. The producer writes to `buffer[tail]` and publishes with `memory_order_release`. The consumer reads with `memory_order_acquire` and advances `head`. Because they operate on opposite ends of a circular array, they never lock each other."
- **Possible Follow-up**: *"What is false sharing and how did you prevent it?"*
- **Follow-up Answer**:
  > "False sharing happens when `head` and `tail` reside on the same 64-byte CPU cache line, causing cores to invalidate each other's caches. We use `alignas(64)` on `head` and `tail` so each sits on its own dedicated cache line."

---

### 14. "Why BSD kqueue for the gateway?"
- **What Interviewer is Testing**: OS event demultiplexers and scalability.
- **Strong Answer**:
  > "`kqueue` is an event-driven notification mechanism in the BSD/macOS kernel. Unlike `select` or `poll` which scan all descriptors in $O(N)$ time, `kqueue` returns active events in $O(1)$ time. It allows a single event loop thread to multiplex thousands of active connections without spinning up thousands of blocking OS threads."
- **Possible Follow-up**: *"How would you deploy this on Linux?"*
- **Follow-up Answer**:
  > "I would replace `kqueue` with Linux `epoll` (`epoll_create`, `epoll_ctl`, `epoll_wait`). The reactor pattern and state machine remain identical."

---

### 15. "How do you handle TCP backpressure?"
- **What Interviewer is Testing**: Socket write safety, non-blocking I/O, and head-of-line blocking.
- **Strong Answer**:
  > "In non-blocking sockets, if a client stops reading, kernel socket buffers fill up and `send()` returns `EAGAIN`. In our `send_all_socket` loop, we retry with `std::this_thread::yield()` up to 500 times. If the client stays blocked after 500 attempts, the gateway tears down the connection and closes the socket. This caps worst-case reactor delay to ~1–5 ms and prevents a slow client from starving others."
- **Possible Follow-up**: *"What happens if the internal command queue fills up?"*
- **Follow-up Answer**:
  > "If the SPSC queue is full, the gateway tracks drops via telemetry and returns an immediate rejection rather than blocking the network reactor thread."

---

### 16. "How does graceful shutdown guarantee no data is lost?"
- **What Interviewer is Testing**: System lifecycle, signal handling, and drain linearization.
- **Strong Answer**:
  > "We follow strict drain linearization:
  > 1. Stop Ingress: Close the listening socket so no new connections enter.
  > 2. Drain Commands: The consumer thread flushes all pending commands from the SPSC queue into the matching engine.
  > 3. Drain Events: The engine emits all final execution events.
  > 4. Project Events: The projector applies all remaining events to the Read Model.
  > 5. Close Connections: Only then are client sockets closed.
  > This ensures `events_pushed == events_processed` with 100% accounting conservation."

---

### 17. "How did you test for race conditions and memory safety?"
- **What Interviewer is Testing**: Sanitizers, testing rigor, and engineering discipline.
- **Strong Answer**:
  > "We compile separate binaries with Clang sanitizers:
  > - **AddressSanitizer + UndefinedBehaviorSanitizer**: Verified 0 memory leaks, 0 heap overflows, and 0 undefined bit shifts across 115 C++ tests.
  > - **ThreadSanitizer (TSan)**: Verified 66 multithreaded tests with 0 data races.
  > - **Stress Simulation**: Ran a 5-million order simulation across 4 instruments verifying full volume and VWAP conservation."
- **Possible Follow-up**: *"Why were TSan runs initially slow during development?"*
- **Follow-up Answer**:
  > "Under TSan's 10x shadow memory overhead, an unbounded traffic loop in a shutdown test was spinning for millions of iterations while waiting for drain. By bounding the traffic burst and signaling stop before calling stop, the test duration dropped from 30 minutes to 2.4 seconds."
