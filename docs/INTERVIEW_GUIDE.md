# Comprehensive SDE Interview Guide

This guide is designed to prepare you to explain and defend the matching engine project naturally, confidently, and deeply in backend software engineering interviews.

---

## Elevator Pitches

### 30-Second Explanation (Memorize This)
> "I built an in-memory limit order book matching engine in C++ exposed via a non-blocking TCP gateway and a Python FastAPI REST interface. It uses a single-threaded matching core with direct-indexed price ladders and hardware bitmap scans to achieve deterministic sub-microsecond matching with zero heap allocations on the hot path. Networking and queries are decoupled from matching using lock-free single-producer single-consumer queues and an in-memory CQRS read model."

---

### 90-Second Explanation
> "The goal of this project was to understand how low-latency financial systems achieve deterministic performance and concurrency safety.
>
> At the core is a single-threaded C++ matching engine that implements standard Price-Time Priority (FIFO). Because matching is strictly sequential per instrument, running a single matching thread eliminates lock contention, mutex overhead, and race conditions entirely.
>
> For data structures, instead of a balanced tree like `std::map`, I used a direct-indexed vector price ladder combined with 64-bit word bitmaps. Finding the best bid or ask takes a single hardware bit-scan instruction (`__builtin_clzll` / `__builtin_ctzll`). Orders are allocated from a pre-reserved pool and chained in intrusive doubly linked lists, allowing $O(1)$ order cancellations.
>
> To handle client traffic without stalling the core, I separated concerns using a CQRS architecture. A non-blocking BSD `kqueue` TCP gateway parses incoming binary wire frames and pushes them into a lock-free SPSC ring buffer with 64-byte cacheline alignment. The engine processes commands, generates monotonic execution events, and emits them to an outbound queue, where a background Projector updates an in-memory Read Model protected by reader-writer locks for concurrent HTTP queries.
>
> The system has 142 automated tests, passes clean under AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer, and processes 5 million simulated orders with full volume and VWAP conservation."

---

### 3-Minute Deep Dive
> "When designing a high-throughput backend service, the primary bottlenecks are usually lock contention, heap allocation overhead, and thread synchronization latency. I wanted to design an electronic exchange prototype that addressed these three bottlenecks directly.
>
> First, on the matching core: Standard implementations use balanced binary search trees like red-black trees or `std::map`. While $O(\log N)$ sounds acceptable, pointer chasing through tree nodes causes frequent cache misses, and dynamic node allocation adds heap latency. Instead, I designed the order book around a flat, pre-allocated vector price array and a 64-bit word bitmap. By tracking active price levels with bit flags, finding the best bid or ask compiles down to hardware bit-scan instructions (`lzcnt`/`tzcnt`), executing in a few CPU cycles. Order cancellation is $O(1)$ constant time because each order holds intrusive pointers within a pre-allocated object pool.
>
> Second, on concurrency: A common anti-pattern is sharing mutexes across network threads, matching logic, and query endpoints. Under heavy load, lock contention destroys throughput. I used a Command Query Responsibility Segregation (CQRS) architecture:
> - The Ingress plane uses a non-blocking event-driven TCP gateway (using BSD `kqueue` on macOS) communicating with clients using a compact binary big-endian protocol.
> - Ingress threads communicate with the matching engine solely through a cacheline-aligned lock-free Single-Producer Single-Consumer (SPSC) queue.
> - The matching core runs single-threaded with zero locks.
> - An outbound SPSC queue feeds a background Projector worker, which updates an in-memory Read Model. Concurrent REST clients querying depth or trade history take shared reader locks on the Read Model, completely isolated from the matching core.
>
> Finally, for reliability: I implemented clean backpressure bounds, graceful shutdown drain linearization (ensuring all queued orders and events are drained before termination), and comprehensive sanitizers. The repository passes AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer without data races or memory leaks, verified across 142 tests and a 5-million order stress simulation."

---

## Core Interview Topics

```
                      TOPIC MAP
                      
         CORE (Must Know Cold)
           ├── Price-Time Priority & Matching
           ├── Order Book Data Structures & Bitmaps
           ├── Order Lifecycle Invariants & States
           └── CQRS Request Flow
           
         SUPPORTING (Should Know)
           ├── Lock-Free SPSC Queues
           ├── Why Single-Threaded Core?
           ├── TCP Gateway & kqueue Reactor
           └── In-Memory Read Model & Projector
           
         ADVANCED (Bonus Points)
           ├── Zero Heap Allocation on Hot Path
           ├── Backpressure & Bounded Sockets
           ├── Shutdown Drain Linearization
           └── Sanitizer Verification & TSan
```

---

### 1. Price-Time Priority & Matching Algorithm

#### Simple Explanation
Orders are matched based on who has the best price. If two people offer the exact same price, the person who submitted their order first gets matched first (First In, First Out).

#### Technical Explanation
- Buy limit orders match against resting asks where $\text{Ask Price} \le \text{Buy Price}$, starting at the lowest ask.
- Sell limit orders match against resting bids where $\text{Bid Price} \ge \text{Sell Price}$, starting at the highest bid.
- At each price level, resting orders form a FIFO queue. The incoming aggressive order fills resting orders sequentially until either the incoming order is completely filled or no compatible price levels remain.
- Unfilled GTC (Good-Til-Cancelled) limit orders rest on their corresponding price level.

#### Why We Chose It
Price-time priority is the industry standard for transparent, fair electronic markets (used on NASDAQ, NYSE, and crypto exchanges).

#### Likely Interviewer Question
> *"What happens when an incoming buy order price is higher than the best resting ask?"*

#### Concise Answer
> "The trade executes at the **resting order's price** (the passive price), not the incoming order's price. The buyer receives price improvement, getting filled at the lower ask price that was already committed to the book."

#### Follow-Up Question & Answer
> **Q**: *"What if the resting order only has 30 shares and the incoming order wants 100?"*
> **A**: *"A partial fill occurs. 30 shares trade immediately at the resting price, the resting order is marked FILLED and removed, and the remaining 70 shares continue matching against the next available price level or rest on the book."*

---

### 2. Order Book Data Structures: Why Bitmaps & Intrusive Lists?

#### Simple Explanation
Instead of searching through a long list or tree of orders, we store prices in a flat table and use 64-bit numbers as 'index tabs' to instantly find where orders exist.

#### Technical Explanation
- **Price Ladder**: Flat `std::vector<PriceLevel>` pre-allocated for discrete prices (0 to 100,000). Direct indexing $O(1)$.
- **Bitmaps**: A vector of 64-bit integers (`uint64_t`) where each bit represents whether a price level has resting volume.
  - Best Ask: `findNextAsk` uses `__builtin_ctzll` (Count Trailing Zeros) to find the lowest set bit in a single CPU instruction.
  - Best Bid: `findNextBid` uses `63 - __builtin_clzll` (Count Leading Zeros) to find the highest set bit.
- **Intrusive Doubly Linked List**: `Order` objects contain embedded `prev` and `next` pointers.
- **Order Lookup Table**: A pre-allocated direct array `orderLookup[order_id] = Order*`.

#### Why We Chose It
`std::map` (Red-Black tree) has $O(\log N)$ lookup and cancellation time, requires dynamic node allocation, and causes pointer chasing. A direct array + bitmap gives $O(1)$ lookup, $O(1)$ cancel, $O(1)$ best price retrieval, and zero cache-line thrashing.

#### Likely Interviewer Question
> *"Why not use `std::unordered_map` for order lookup and price levels?"*

#### Concise Answer
> "`std::unordered_map` does not maintain price ordering, so finding the best bid or ask would require scanning all keys ($O(N)$). Hash collisions also cause unpredictable tail latency spikes. Direct-indexed vectors provide deterministic, sub-microsecond $O(1)$ access without hashing overhead."

#### Follow-Up Question & Answer
> **Q**: *"What is the memory cost of pre-allocating 100,000 price levels?"*
> **A**: *"Each `PriceLevel` is 24 bytes (head pointer, tail pointer, total volume). For 100,000 levels on bids and asks, total memory is under 5 megabytes—negligible on modern servers, but providing massive cache predictability."*

---

### 3. O(1) Cancellation Mechanism

#### Simple Explanation
When a user cancels an order, we look up its address directly in a table and remove it from its list in one step, without scanning through other orders.

#### Technical Explanation
1. `Order* node = orderLookup[order_id]` ($O(1)$ direct array index).
2. If `node == nullptr`, order doesn't exist or was already filled.
3. Access `PriceLevel& level = (node->side == Buy) ? bids[node->price] : asks[node->price]`.
4. Intrusive doubly linked list unlink:
   ```cpp
   if (node->prev) node->prev->next = node->next;
   else level.head = node->next;
   if (node->next) node->next->prev = node->prev;
   else level.tail = node->prev;
   ```
5. Decrement `level.totalVolume -= node->remaining_qty`.
6. If `level.totalVolume == 0`, clear bit in bitmap (`clearBit`).
7. Recycle node back to `OrderPool::deallocate(node)` ($O(1)$ push to vector free-list).
8. Set `orderLookup[order_id] = nullptr`.

#### Likely Interviewer Question
> *"What benchmark proved your O(1) cancellation was faster than naive list scanning?"*

#### Concise Answer
> "In our Google Benchmark suite, under order book contention with hundreds of resting orders, naive sequential cancellation took 8.3 microseconds. Our O(1) pointer unlink took 3.5 microseconds—a 2.34x speedup that remains constant regardless of order book depth."

---

### 4. Order Lifecycle Invariants

#### Simple Explanation
Every share of an order must always be accounted for: it is either executed in a trade, still waiting on the book, or cancelled.

#### Technical Explanation
Across all state transitions, the conservation equation strictly holds:
$$\text{original\_qty} = \text{filled\_qty} + \text{remaining\_qty} + \text{cancelled\_qty}$$

- **`New`**: `rem = orig`, `filled = 0`, `cancelled = 0`.
- **`PartiallyFilled`**: `rem > 0`, `filled = orig - rem`, `cancelled = 0`.
- **`Filled`**: `rem = 0`, `filled = orig`, `cancelled = 0`.
- **`Cancelled`**: `rem = 0`, `filled = pre_cancel_filled`, `cancelled = orig - filled`.
- **`Rejected`**: `rem = 0`, `filled = 0`, `cancelled = orig`.

#### Key Invariant Rules
1. `filled_qty` is monotonically non-decreasing.
2. Terminal states (`Filled`, `Cancelled`, `Rejected`) can never be regressed by delayed or out-of-order network updates.

---

### 5. Why Single-Threaded Matching Core?

#### Simple Explanation
Matching orders for a stock is fundamentally sequential. Trying to make it multi-threaded with locks slows it down because threads spend all their time waiting on each other.

#### Technical Explanation
- A limit order book is a shared state machine: Order B's fill depends entirely on whether Order A matched first.
- If multiple threads try to mutate the same order book, you need coarse-grained mutexes or complex lock-free data structures. Under high write contention, cache coherence traffic (cache invalidation loops) degrades latency by orders of magnitude.
- By dedicating **one single thread** to matching an instrument, all matching operations are lock-free and wait-free. Memory sits hot in the L1/L2 CPU cache.
- Multi-core scaling is achieved by **partitioning across instruments** (e.g. Thread 1 matches AAPL, Thread 2 matches RELIANCE), not by multi-threading a single order book.

#### Likely Interviewer Question
> *"If the matching core is single-threaded, how does the system scale to handle thousands of users?"*

#### Concise Answer
> "We decouple I/O from matching using a CQRS architecture. Network ingestion and JSON serialization happen across multiple threads in the TCP gateway and FastAPI layer. Validated commands are passed to the matching thread via lock-free ring buffers. The matching thread does only CPU-bound matching without touching network sockets."

---

### 6. Lock-Free SPSC Queue & Cache-Line Isolation

#### Simple Explanation
It's a circular array where one thread only writes to the tail, and one thread only reads from the head. Because they touch different ends, they don't block each other.

#### Technical Explanation
- Single-Producer Single-Consumer (SPSC) circular ring buffer.
- `head` and `tail` are atomic indices.
  - Producer writes `buffer[tail]` and executes `tail.store(next, std::memory_order_release)`.
  - Consumer reads `tail` with `memory_order_acquire`, reads `buffer[head]`, and executes `head.store(next, std::memory_order_release)`.
- **Cache-Line Isolation**:
  `alignas(64) std::atomic<size_t> head;`
  `alignas(64) std::atomic<size_t> tail;`
  Prevents **false sharing**, where two CPU cores constantly invalidate each other's L1 cache line even though they are modifying different variables.

#### Likely Interviewer Question
> *"Why not use an MPSC (Multi-Producer Single-Consumer) queue if multiple network connections exist?"*

#### Concise Answer
> "In our design, the TCP Gateway runs a single-threaded event loop (kqueue) that multiplexes all client connections. Therefore, there is only one producer pushing into the command queue and one consumer (the matching engine) popping from it. An SPSC queue requires only acquire-release memory fences and avoids expensive atomic CAS (Compare-And-Swap) loops required by MPSC queues."

---

### 7. Graceful Shutdown & Drain Linearization

#### Simple Explanation
When the server stops, it doesn't drop orders or corrupt state. It stops accepting new clients, processes all in-flight orders in the queue, updates the read model, and then exits cleanly.

#### Technical Explanation
Shutdown follows a strict linear sequence:
1. **Stop Ingress**: `gateway.stop()` closes the listening socket and halts the `kqueue` loop. No new connections or frames are accepted.
2. **Signal In-Flight Clients**: Atomic `stop_traffic` flags signal client threads to stop sending.
3. **Drain Command Queue**: The consumer thread drains all remaining commands from the SPSC queue into the matching engine.
4. **Drain Outbound Events**: The engine finishes processing and signals the outbound queue.
5. **Project Remaining Events**: `Projector::stop()` drains all execution events (`Trade`, `OrderState`, `L2Update`) into the `ReadModel`.
6. **Reclaim Sockets**: All client sockets are closed cleanly.
- **Conservation Verified**: `events_pushed == events_processed` with zero memory leaks.

---

### 8. Sanitizers & Bounded Concurrency Testing

#### Simple Explanation
We compile the code with special compiler tools that catch memory bugs, undefined behavior, and multi-threaded race conditions at runtime.

#### Technical Explanation
- **AddressSanitizer (ASan)**: Detects heap buffer overflows, use-after-free, and memory leaks.
- **UndefinedBehaviorSanitizer (UBSan)**: Detects integer overflows, bit shifts past word sizes (e.g. `1ULL << 64`), and alignment faults.
- **ThreadSanitizer (TSan)**: Instruments memory accesses and locks with shadow memory to catch data races.
- **Strict Bounded Execution**:
  Because TSan has an 8x–10x performance overhead, concurrency tests must have deterministic bounded iteration counts and explicit timeouts rather than unbounded polling loops.
  - `test_read_model_tsan`: 24 tests passed in **7.01s**.
  - `test_gateway_tsan`: 42 tests passed in **24.53s**.
  - 0 data races, 0 deadlocks across 66 concurrency tests.

---

### 9. What I Would Build Next (Future Improvements)

If an interviewer asks *"What are the limitations and what would you build next?"*, give this structured answer:

1. **Write-Ahead Logging (WAL) & Persistence**:
   Currently, the engine is in-memory only. I would add an append-only WAL using memory-mapped files (`mmap`) with asynchronous fsync to allow state reconstruction upon restart.
2. **Linux `epoll` Port**:
   The current gateway uses BSD `kqueue` for macOS. I would introduce an abstraction layer supporting `epoll` for production Linux deployments.
3. **Multi-Instrument Sharding**:
   Spin up dedicated matching engine worker threads per financial instrument to scale horizontally across multi-socket server hardware.
4. **FIX Protocol / WebSocket Gateway**:
   Support industry-standard Financial Information eXchange (FIX) protocol for institutions and WebSocket streaming for real-time order book level 2 feeds.
