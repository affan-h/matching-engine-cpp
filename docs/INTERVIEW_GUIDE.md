# Master SDE Interview Guide: Order Matching Service

*A structured, conversational guide for software engineering interviews. Starts simple, builds intuitive mental models, and deepens only when probed.*

---

## 1. The Pitches

### The 30-Second Pitch (Conversational & Natural)
> "I built an in-memory order matching service in C++. Clients can submit buy and sell orders through a REST API. The matching engine maintains an order book and matches compatible orders using price-time priority.
>
> For example, if someone places a buy order for 100 shares at $150 and another client sells 40 shares at $150, the engine executes a 40-share trade and leaves 60 shares resting in the book.
>
> I separated networking and read queries from the core matching logic so the matching path stays simple and deterministic."

#### If the Interviewer asks: "What makes it technically interesting?"
> "A few key things:
> 1. **Data structures**: Instead of a tree, I used direct-indexed arrays and 64-bit bitmaps so finding the best price takes one hardware bit-scan instruction, and order cancellation is strict O(1).
> 2. **Concurrency**: The matching core is single-threaded with zero locks, decoupled from the network gateway via lock-free ring buffers.
> 3. **Reliability**: It passes AddressSanitizer and ThreadSanitizer cleanly across 142 tests and a 5-million order simulation."

---

### The 90-Second Pitch (The Narrative Arc)
> "The problem I wanted to explore was how high-throughput trading systems achieve predictable, microsecond-scale latency while staying safe under concurrent traffic.
>
> To solve this, I built an in-memory order matching service in C++ with a Python FastAPI REST interface.
>
> When an order comes in through the API, it is converted into a compact binary packet and sent over TCP to a gateway, which queues it for the matching core.
>
> The matching core checks the order book using standard Price-Time Priority: better prices match first, and orders at the same price are matched in FIFO order. If a compatible opposite order exists, a trade occurs immediately; otherwise, the order rests in the book.
>
> For data structures, instead of searching every order when cancelling, I keep a direct index from order ID to the order object, using intrusive linked-list pointers so cancellation can unlink the order directly in O(1) constant time.
>
> To keep the matching core fast, I separated networking and query traffic from matching. The matching engine runs on its own thread without taking locks, and execution events are projected to an in-memory read model that serves client queries.
>
> I verified the system with 142 automated tests, ran it clean under Clang AddressSanitizer and ThreadSanitizer, and benchmarked order matching at around 4 microseconds."

---

### The 3-Minute Deep Dive (The Complete End-to-End Story)
> "Let me walk you through the end-to-end architecture by tracing an order from the client all the way through the system:
>
> 1. **Client & REST API**: A client submits an order via HTTP, like `POST /orders` to buy 100 shares of Apple at $150. FastAPI validates the request schema and returns a 202 Accepted response with a correlation ID.
> 2. **TCP Gateway**: Underneath, FastAPI forwards the order over a local TCP connection as a compact 17-byte binary frame to a C++ gateway. The gateway uses non-blocking sockets to receive frames from multiple clients without blocking.
> 3. **Command Queue**: The gateway pushes the incoming order into an in-memory ring buffer called an SPSC queue. This hands the order to the matching engine without either thread needing to lock the other.
> 4. **Matching Engine Core**: A dedicated matching thread pops the order and checks the Apple order book.
> 5. **Order Book & Matching**: The engine checks for compatible sellers. If there is a resting seller at $150 or below, a trade executes immediately at the resting price. If there are no sellers, or if shares remain, the order rests in the book at price level $150.
> 6. **Outbound Event Queue**: Whenever a trade, order state change, or book depth change occurs, the engine emits an execution event into an outbound queue.
> 7. **Read Model & Projector**: A background worker thread called the Projector reads these events and updates an in-memory Read Model.
> 8. **Queries**: When users refresh their dashboard or query `GET /book/AAPL` or `GET /trades/AAPL`, they read from this Read Model using shared read locks, completely isolated from the matching engine.
>
> Now, why is it designed this way?
> - **Why single-threaded core?** Order matching for a single stock is fundamentally sequential. Running a single matching thread eliminates lock contention, mutex overhead, and race conditions.
> - **Why SPSC queues?** Ring buffers let the network thread and the matching thread communicate with simple atomic memory ordering, avoiding kernel sleep/wake transitions.
> - **Why a separate Read Model?** If thousands of dashboard users querying market depth had to lock the active order book, matching latency would spike. CQRS keeps the hot path clear.
> - **Why flat arrays and bitmaps?** Instead of an $O(\log N)$ tree that causes cache misses, flat price arrays give $O(1)$ access, and 64-bit word bitmaps find the best bid or ask in a single CPU instruction (`lzcnt`/`tzcnt`)."

---

## 2. Core Technical Concepts (Teaching Progression)

*Every concept below follows: Simple Explanation $\rightarrow$ Why It Exists $\rightarrow$ How We Use It $\rightarrow$ Technical Details $\rightarrow$ Likely Interviewer Question.*

---

### Concept 1: Price-Time Priority (FIFO)
- **Simple**: Better price wins first. If prices are identical, the older order wins first.
- **Why It Exists**: It is the industry-standard rule for electronic stock exchanges because it rewards aggressive price setting and fairness.
- **How We Use It**: Bids match against lowest asks; asks match against highest bids. Within a price level, orders are matched from oldest to newest.
- **Technical Details**:
  - A buy order at \$150 matching against a resting ask at \$149 executes at **\$149** (passive resting price improvement).
  - Trades consume liquidity from the head of the price level's queue.
- **Likely Interviewer Question**:
  > *"What happens if a buyer submits an order for 100 shares at \$150, but the best ask only has 30 shares?"*
  > **Answer**: "A partial fill occurs: 30 shares trade immediately at the resting price, the resting ask is marked FILLED and removed, and the remaining 70 shares continue matching against higher price levels or rest in the book."

---

### Concept 2: Order Book Data Structures & Bitmaps
- **Simple**: Instead of searching through a big tree or list to find the best price, we keep prices in a flat array and use 64-bit numbers as 'tabs' to mark where orders exist.
- **Why It Exists**: In standard trees like `std::map`, searching takes $O(\log N)$ and jumping through pointers causes CPU cache misses.
- **How We Use It**: We allocate an array of price levels from 1 to 100,000. When an order is added, we set the corresponding bit in a 64-bit word bitmap.
- **Technical Details**:
  - `findNextAsk` masks lower bits and calls `__builtin_ctzll` (count trailing zeros).
  - `findNextBid` masks higher bits and calls `__builtin_clzll` (count leading zeros).
  - Compiles to single CPU hardware instructions (`tzcnt`/`lzcnt`).
- **Likely Interviewer Question**:
  > *"What is the memory footprint of pre-allocating 100,000 price levels?"*
  > **Answer**: "Each `PriceLevel` is 24 bytes (head, tail, volume). For 100,000 levels across bids and asks, total memory is under 5 megabytes—negligible on modern servers, but providing massive cache predictability."

---

### Concept 3: O(1) Cancellation with Intrusive Lists
- **Simple**: When someone cancels an order, we find its memory address directly from a lookup table and unlink it in one step, without searching through other orders.
- **Why It Exists**: If an order book has thousands of orders, scanning a list sequentially to find an order takes $O(N)$ time.
- **How We Use It**: Each `Order` struct contains embedded `prev` and `next` pointers. An array `orderLookup[order_id]` points directly to the `Order`.
- **Technical Details**:
  - `Order* node = orderLookup[id];`
  - Unlink: `node->prev->next = node->next; node->next->prev = node->prev;`
  - Decrement price level volume; if 0, clear bit in bitmap.
  - Return `node` to the pre-allocated pool free-list.
- **Likely Interviewer Question**:
  > *"Did you measure whether O(1) cancellation actually mattered?"*
  > **Answer**: "Yes. In our Google Benchmark suite under high order book contention, naive sequential scanning took 8.3 microseconds, while our O(1) pointer unlink took 3.5 microseconds—a 2.34x speedup that stays constant regardless of book depth."

---

### Concept 4: SPSC Queues
- **Simple**: A circular pipe connecting two threads where one thread only writes to the tail, and the other thread only reads from the head.
- **Why It Exists**: It lets the network gateway pass orders to the matching engine without using expensive mutexes or locks.
- **How We Use It**: The network gateway pushes commands into `command_queue`; the matching engine pops them. The engine pushes execution events into `outbound_queue`; the projector pops them.
- **Technical Details**:
  - Single-Producer Single-Consumer circular ring buffer.
  - Uses `atomic` head and tail with acquire/release memory ordering.
  - Padded with `alignas(64)` to place head and tail on separate CPU cache lines, eliminating **false sharing**.
- **Likely Interviewer Question**:
  > *"Why not use a Multi-Producer Single-Consumer (MPSC) queue?"*
  > **Answer**: "Because our TCP Gateway multiplexes all client connections onto a single event-loop thread. Therefore, there is only one producer thread pushing into the queue, so an SPSC queue is sufficient and avoids the expensive atomic CAS retry loops required by MPSC queues."

---

### Concept 5: CQRS Read Model & Reader-Writer Locks
- **Simple**: We keep the 'matching' copy of the order book completely separate from the 'querying' copy.
- **Why It Exists**: If hundreds of people refreshing their browser dashboards had to lock the active order book, incoming trade execution would be delayed.
- **How We Use It**: The matching engine runs on its own thread and emits execution events. A background worker updates an in-memory Read Model. REST clients query the Read Model.
- **Technical Details**:
  - Read Model uses `std::shared_mutex`.
  - Dashboard queries take a shared read lock (`std::shared_lock`), allowing hundreds of concurrent readers.
  - The Projector worker takes an exclusive write lock (`std::unique_lock`) only when applying events.
- **Likely Interviewer Question**:
  > *"Does this introduce eventual consistency?"*
  > **Answer**: "Yes, there is an ultra-low latency eventual consistency window—typically a few microseconds—between when an order matches and when it appears in the read model. Within the matching engine itself, state is 100% strictly consistent and sequential."

---

### Concept 6: Order Lifecycle Invariant
- **Simple**: Every share of an order must always be accounted for: executed, waiting on the book, or cancelled.
- **Why It Exists**: Prevents silent quantity corruption, double fills, or phantom shares.
- **How We Use It**: Verified mathematically across all state transitions:
  $$\text{original\_qty} = \text{filled\_qty} + \text{remaining\_qty} + \text{cancelled\_qty}$$
- **Technical Details**:
  - `filled_qty` is monotonically non-decreasing.
  - Terminal states (`FILLED`, `CANCELLED`, `REJECTED`) cannot be regressed by delayed or out-of-order network updates.

---

### Concept 7: Sanitizers & Concurrency Verification
- **Simple**: Compiler diagnostic tools that monitor running code to catch memory bugs and multi-threaded race conditions.
- **Why It Exists**: Concurrency bugs and memory leaks are notoriously difficult to catch with simple unit tests because they depend on thread timing.
- **How We Use It**:
  - **AddressSanitizer / UndefinedBehaviorSanitizer**: 115 tests clean, 0 memory leaks, 0 buffer overflows, 0 undefined bit shifts.
  - **ThreadSanitizer (TSan)**: 66 multithreaded tests clean, 0 data races, running in 31.5 seconds.
- **Likely Interviewer Question**:
  > *"Did ThreadSanitizer help you find any real bugs during development?"*
  > **Answer**: "Yes. TSan exposed a test where client threads generated traffic in an unbounded loop while the gateway was draining. Because the stop flag was set after `gateway.stop()`, the drain loop spun for minutes under TSan instrumentation. By bounding the burst and signaling stop before initiating shutdown, the test runtime dropped from 30 minutes to 2.4 seconds with zero races."
