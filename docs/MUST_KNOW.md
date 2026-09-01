# What I Actually Need to Know

You do NOT need to memorize the entire codebase. Focus your preparation on these three tiers.

---

## Tier 1: MUST KNOW COLD (Be Ready to Explain Without Thinking)

1. **What the Service Does in One Sentence**:
   - "An in-memory electronic order matching service written in C++ with a Python FastAPI REST interface, matching orders using price-time priority."
2. **The Request Flow**:
   - Client sends HTTP order -> FastAPI serializes to binary -> TCP gateway queues it -> Matching Engine matches or rests it -> Projector updates in-memory Read Model -> Client queries depth/trades.
3. **Price-Time Priority (FIFO)**:
   - Better price matches first (higher buyers, lower sellers).
   - Same price matches oldest order first.
   - Passive resting price is the execution price.
4. **Order Types & Time-in-Force**:
   - **Limit Order**: Buy/sell at specified price or better.
   - **Market Order**: Executes immediately at whatever prices are available.
   - **GTC (Good-Til-Cancelled)**: Default limit order, rests on book until filled or cancelled.
   - **IOC (Immediate-Or-Cancel)**: Fills whatever is available immediately; cancels the rest.
   - **FOK (Fill-Or-Kill)**: Fills the entire requested quantity immediately or rejects completely with 0 fills.
5. **Why the Matching Core is Single-Threaded**:
   - Order matching is inherently sequential for a given instrument. Running a single thread eliminates lock contention, mutex overhead, and race conditions.
6. **Order Book Data Structures**:
   - Direct array for price levels (`bids[price]`).
   - 64-bit word bitmaps for finding best prices via CPU bit-scan instructions.
   - Intrusive doubly linked list + direct pointer lookup for $O(1)$ order cancellation.
7. **Order Conservation Equation**:
   - $\text{original\_qty} = \text{filled\_qty} + \text{remaining\_qty} + \text{cancelled\_qty}$.

---

## Tier 2: SHOULD KNOW (Understand Well and Explain if Asked)

1. **CQRS Architecture**:
   - Command Plane (matching) is separated from Query Plane (ReadModel).
   - Why: Prevents dashboard queries from locking the active order book and slowing down trade execution.
2. **Lock-Free SPSC Ring Buffer**:
   - Single-Producer Single-Consumer circular buffer.
   - Producer writes tail (`release`), consumer reads tail (`acquire`) and advances head.
   - `alignas(64)` cache-line alignment prevents false sharing.
3. **TCP Wire Protocol**:
   - Big-endian binary frame: 2-byte length, 1-byte message type, followed by fixed struct fields (17 to 25 bytes total).
   - Far faster and smaller than parsing JSON in C++.
4. **Client Correlation vs Idempotency**:
   - The engine assigns a monotonically increasing unique `order_id` to every placed order.
   - `client_order_id` is a client-provided correlation token indexed by the Read Model so clients can safely query `GET /orders/{client_order_id}?by_client_id=true` before retrying after a network timeout.
5. **Sanitizers Used**:
   - ASan (memory leaks, buffer overflows), UBSan (undefined behavior like bit shifts), TSan (data races).
   - 142 automated tests, 0 leaks, 0 races.
6. **Benchmark Latencies**:
   - Insert: ~5.5 µs, Match: ~4.1 µs, Cancel: ~3.6 µs.
   - $O(1)$ cancel is 2.34x faster than naive linked-list search under contention.
7. **Known Limitations**:
   - Discrete price space (1 to 100,000).
   - Single-threaded matching core.
   - BSD kqueue (macOS only, would use epoll for Linux).
   - Pure in-memory (no disk WAL persistence).

---

## Tier 3: NICE TO KNOW (Deep Details if the Interviewer Goes Deep)

1. **The 64-bit Shift Edge Case**:
   - In `findNextBid`, `(1ULL << (bit + 1)) - 1` when `bit == 63` shifts by 64 bits, which is undefined behavior in C++. We guard this with `(bit == 63) ? ~0ULL : ...`.
2. **Socket Backpressure Handling**:
   - `send_all_socket` yields up to 500 times on `EAGAIN`/`EWOULDBLOCK`. If the client socket stays full, the connection is closed to prevent reactor starvation.
3. **Shutdown Linearization Sequence**:
   - Stop accepting -> Drain command queue -> Stop engine -> Drain outbound queue -> Project remaining events -> Close client connections.
4. **Order Pool Implementation**:
   - Pre-allocated `std::vector<Order> pool(2'000'000)` with `std::vector<Order*> freeList` populated in reverse order for cache locality.
5. **ReadModel Reader-Writer Lock**:
   - Uses `std::shared_mutex`. Readers take `std::shared_lock`, writer (Projector) takes `std::unique_lock`.
   - Protects against state regression (delayed updates cannot revert `Filled` or `Cancelled` back to `New`).
