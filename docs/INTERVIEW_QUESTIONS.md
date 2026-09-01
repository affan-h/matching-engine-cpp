# Project Interview Question Bank

This question bank contains realistic technical interview questions based directly on the actual implementation of this matching engine project.

---

## 1. Basic Project Questions

### Q1: Can you walk me through high-level what your project does?
- **What Interviewer is Testing**: Communication clarity, ability to explain a technical system without drowning in buzzwords.
- **Strong Answer**:
  > "It is an in-memory electronic order matching service written in C++ with a Python FastAPI REST interface. Clients submit limit or market orders, the engine matches compatible buy and sell orders using price-time priority, and any unfilled quantity rests in an order book. As trades occur, events are asynchronously projected into an in-memory read model that allows concurrent HTTP clients to query live depth, trade logs, and order statuses without pausing the matching engine."
- **Possible Follow-up**: *"Why did you build both a C++ core and a Python REST API instead of doing everything in one language?"*
- **Follow-up Answer**:
  > "It separates performance-critical logic from developer ergonomics. The matching engine and TCP gateway require deterministic sub-microsecond latency and memory control, which C++ excels at. Meanwhile, Python with FastAPI provides fast, expressive schema validation, OpenAPI documentation, and standard HTTP integration for client applications."

---

## 2. C++ Questions

### Q2: How did you ensure zero heap allocations on the matching hot path?
- **What Interviewer is Testing**: Understanding of dynamic memory overhead, cache efficiency, and low-latency C++ idioms.
- **Strong Answer**:
  > "Every data structure on the critical matching path is pre-allocated during engine initialization. We use an `OrderPool` with a pre-sized vector of 2 million `Order` structs and a vector-backed free-list. The price ladder is a flat vector of 100,001 levels, and the order lookup table is a pre-allocated pointer array. When an order is placed, cancelled, or matched, memory is acquired and returned to the pool using pointer arithmetic without calling `malloc` or `new`."
- **Possible Follow-up**: *"What happens if the order pool runs out of memory?"*
- **Follow-up Answer**:
  > "If the free-list is exhausted, the engine safely returns `nullptr` and emits an explicit `RejectCode::QueueFull` or rejection event rather than crashing, ensuring safe failure degradation."

---

## 3. Data Structures

### Q3: Why did you use an intrusive doubly linked list instead of `std::list`?
- **What Interviewer is Testing**: Cache locality, node allocation overhead, and pointer management.
- **Strong Answer**:
  > "Standard `std::list` allocates a separate heap wrapper node for every inserted element, which introduces pointer chasing, heap allocation latency, and memory fragmentation. In an intrusive list, the `prev` and `next` pointers reside directly inside the `Order` struct itself. This means unlinking an order during cancellation is a simple pointer swap on pre-existing memory, requiring zero dynamic allocations and keeping the node's fields contiguous in cache."
- **Possible Follow-up**: *"How do you maintain O(1) order cancellation?"*
- **Follow-up Answer**:
  > "We maintain a direct-indexed array `orderLookup[order_id]`. When a cancel request arrives, we fetch the `Order*` in $O(1)$, unlink its `prev` and `next` pointers from its price level in $O(1)$, decrement the level's volume, and push the node back to the pool free-list."

---

## 4. Algorithms

### Q4: How does your bitmap traversal work for finding the best bid or ask?
- **What Interviewer is Testing**: Bit manipulation, hardware acceleration, and algorithmic complexity.
- **Strong Answer**:
  > "We map price levels (0 to 100,000) to a flat array of 64-bit integers (`uint64_t`). If a price level has resting volume, its bit is set to 1; otherwise 0. To find the lowest active ask, `findNextAsk` masks out lower bits of the current word and calls the compiler built-in `__builtin_ctzll` (count trailing zeros). To find the highest active bid, `findNextBid` masks higher bits and calls `__builtin_clzll` (count leading zeros). These built-ins compile directly into single CPU instructions (`tzcnt`/`lzcnt` on x86, `rbit`+`clz` on ARM), finding the best price in constant hardware instruction time."
- **Possible Follow-up**: *"Did you encounter any edge cases with bit shifting?"*
- **Follow-up Answer**:
  > "Yes. When masking higher bits in a 64-bit word, shifting `1ULL << (bit + 1)` when `bit == 63` invokes undefined behavior in C++ because shifting by 64 bits exceeds the operand width. We explicitly guard `bit == 63` to use `~0ULL`, preventing compiler UB and subtle word-boundary bugs."

---

## 5. Backend / API

### Q5: Why does `POST /orders` return HTTP 202 Accepted instead of 200 OK?
- **What Interviewer is Testing**: Understanding of asynchronous REST design, CQRS, and eventual consistency.
- **Strong Answer**:
  > "Because order submission is asynchronous. The REST API validates the request schema and forwards the binary frame to the TCP gateway's command queue. At the moment HTTP response headers are returned, the matching engine has accepted the command for processing, but matching and fill generation occur asynchronously in the matching thread. Returning 202 Accepted accurately communicates that the command is accepted for processing, and the client can query its final status via `GET /orders/{client_order_id}`."
- **Possible Follow-up**: *"How does the client know when the order has actually executed?"*
- **Follow-up Answer**:
  > "Clients poll `GET /orders/{client_order_id}?by_client_id=true` or query `GET /trades/{symbol}`. In a full production system, we would also expose a WebSocket or Server-Sent Events (SSE) stream for instant push notifications."

---

## 6. Networking

### Q6: Why use BSD `kqueue` instead of blocking threads or `select`?
- **What Interviewer is Testing**: Event-driven I/O models, scalability, and OS primitives.
- **Strong Answer**:
  > "`kqueue` provides an $O(1)$ event-driven notification mechanism in the BSD/macOS kernel. In contrast, `select` or `poll` requires passing and scanning the entire file descriptor set on every iteration ($O(N)$), which degrades severely as connection counts grow. Dedicated blocking threads per connection waste thread stack memory and introduce heavy OS thread context switching. `kqueue` allows a single event-loop thread to efficiently multiplex thousands of active connections."
- **Possible Follow-up**: *"How would you deploy this service on Linux?"*
- **Follow-up Answer**:
  > "Linux uses `epoll` instead of `kqueue`. The reactor design is conceptually identical: `epoll_create`, `epoll_ctl`, and `epoll_wait`. I would wrap the reactor in a lightweight platform abstraction layer (`EventDemultiplexer`) with `kqueue` on macOS and `epoll` on Linux."

---

## 7. Concurrency

### Q7: Why use an SPSC queue between the Gateway and the Engine?
- **What Interviewer is Testing**: Lock-free programming, concurrency boundaries, and cacheline isolation.
- **Strong Answer**:
  > "The TCP Gateway runs a single-threaded `kqueue` event loop that multiplexes incoming client frames, acting as the single Producer. The Matching Engine runs on a dedicated worker thread, acting as the single Consumer. Because there is exactly one producer and one consumer, we use a lock-free Single-Producer Single-Consumer (SPSC) ring buffer. It requires only lightweight acquire-release memory orderings and completely avoids the expensive Compare-And-Swap (CAS) retry loops required by multi-producer queues."
- **Possible Follow-up**: *"What is false sharing, and how did you prevent it in your queue?"*
- **Follow-up Answer**:
  > "False sharing occurs when two independent atomic variables (like the queue's `head` and `tail`) reside on the same 64-byte CPU cache line. When one core writes to `tail`, the other core's cache line containing `head` is invalidated, causing unnecessary cache-miss stalls. We use `alignas(64)` on `head` and `tail` so they reside on separate cache lines, allowing both cores to read and write without cache thrashing."

---

## 8. System Design

### Q8: How does your CQRS pattern separate matching from queries?
- **What Interviewer is Testing**: Architectural decomposition, scaling bottlenecks, and read/write separation.
- **Strong Answer**:
  > "In high-throughput trading, write operations (orders, cancels) must execute with minimal latency. If read queries (market depth, trade history) shared the same order book lock, heavy dashboard traffic would starve the matching core. With CQRS, the matching core writes monotonic execution events to an outbound queue. A background Projector consumes these events and updates an independent in-memory Read Model. REST readers acquire shared read locks (`std::shared_lock`), allowing concurrent queries without ever contending with the matching engine."
- **Possible Follow-up**: *"Does this introduce eventual consistency?"*
- **Follow-up Answer**:
  > "Yes, there is an ultra-low latency eventual consistency window (typically a few microseconds) between when a trade matches and when it appears in the read model. However, within the matching core itself, state is 100% strictly consistent and sequential."

---

## 9. Testing & Sanitizers

### Q9: How did you verify your concurrency safety?
- **What Interviewer is Testing**: Testing rigor, sanitizer experience, and debugging multithreaded systems.
- **Strong Answer**:
  > "We use a multi-tiered testing strategy:
  > 1. **Unit & Protocol Tests**: 142 automated tests covering order edge cases, boundary prices, fragmented TCP packets, and FOK/IOC rules.
  > 2. **Sanitizers**: We compile with Clang using AddressSanitizer and UndefinedBehaviorSanitizer to verify zero leaks and zero memory corruption.
  > 3. **ThreadSanitizer (TSan)**: We run multi-threaded integration suites under TSan to detect data races. All 66 multithreaded tests run clean with zero data races.
  > 4. **Stress Simulation**: A 5-million order multi-threaded simulation verifying full volume and VWAP conservation."
- **Possible Follow-up**: *"What was the most difficult concurrency bug you caught and fixed?"*
- **Follow-up Answer**:
  > "During TSan testing, we discovered a test that previously took 30 minutes to finish. Client threads were spinning in an unbounded traffic loop and the stop flag was set *after* `gateway.stop()` was called. Because `gateway.stop()` was waiting to drain the queue, client threads generated millions of extra orders while draining. By bounding the burst and signaling the stop flag before calling stop, the test duration dropped from 1,807 seconds down to 2.4 seconds with zero races."

---

## 10. Performance

### Q10: What are the benchmark numbers and what do they represent?
- **What Interviewer is Testing**: Ability to interpret benchmarks objectively without fabricating claims.
- **Strong Answer**:
  > "Using Google Benchmark on -O3 compiled code:
  > - Limit Order Insertion: **~5.5 microseconds** ($O(1)$ direct vector lookup + intrusive node insertion).
  > - Order Matching: **~4.1 microseconds** ($O(1)$ bitmap scan + trade generation).
  > - Order Cancellation: **~3.6 microseconds** ($O(1)$ linked-list unlink).
  > - Contended Cancellation: **~3.5 microseconds** vs **8.3 microseconds** for naive sequential scanning—a **2.34x speedup** that remains constant regardless of order book depth."
- **Possible Follow-up**: *"Why does order insertion take ~5 microseconds?"*
- **Follow-up Answer**:
  > "That duration includes object pool retrieval, price ladder indexing, intrusive pointer stitching, bitmap bit setting, and emitting an outbound event frame into the ring buffer. It provides a highly predictable, constant-time execution profile without dynamic memory allocations."

---

## 11. Failure Handling & Backpressure

### Q11: What happens if a connected TCP client stops reading responses?
- **What Interviewer is Testing**: TCP backpressure, non-blocking socket safety, and head-of-line blocking.
- **Strong Answer**:
  > "In non-blocking sockets, if a client stops reading, its kernel socket buffer fills up and subsequent writes return `EAGAIN` or `EWOULDBLOCK`. In our `send_all_socket` loop, we allow up to 500 retry yields. If the client socket remains saturated after 500 attempts, the gateway aborts the write, unregisters the socket from `kqueue`, and closes the connection. This prevents a slow or stalled client from starving the reactor event loop for other participants."
- **Possible Follow-up**: *"What is the worst-case delay that 500 yields can cause?"*
- **Follow-up Answer**:
  > "Under normal multi-threaded CPU scheduling, 500 yields take at most 1 to 5 milliseconds. This caps the worst-case stall to a deterministic bound before the broken client is reclaimed."
