# Resume Project Descriptions & Bullets

*Choose between the General SDE version (recommended for most backend/software engineering roles) and the Systems-Heavy version (recommended for infrastructure, low-latency, or HFT roles).*

---

## Project Header

**In-Memory Order Matching Service (C++ / Python)**

### 1-Line Summary
> Designed and built an in-memory electronic order matching service in C++ with a Python FastAPI REST interface, achieving microsecond-scale execution and O(1) order cancellation.

---

## Option 1: RECOMMENDED FOR GENERAL SDE

*Focuses on backend engineering, data structures, concurrency, and testing without overwhelming the recruiter with niche jargon.*

- **Built an in-memory order matching service in modern C++ (C++17)**: Implemented fair price-time priority (FIFO) matching across multiple instruments with zero dynamic heap allocations on the critical path.
- **Optimized order book data structures for constant-time operations**: Designed direct-indexed price ladders and intrusive doubly linked lists, achieving strict $O(1)$ order cancellation and demonstrating a 2.34x speedup (3.5 µs vs 8.3 µs) over naive list scanning.
- **Architected a concurrent backend using CQRS and thread isolation**: Separated high-frequency order matching from client queries by decoupling networking and matching threads via in-memory ring buffers, exposing live depth and trade logs through a Python FastAPI REST API.
- **Enforced production-grade reliability and test coverage**: Authored 142 automated unit and integration tests, verified zero memory leaks or data races under Clang AddressSanitizer and ThreadSanitizer, and validated volume conservation across a 5-million order stress simulation.

---

## Option 2: SYSTEMS-HEAVY ALTERNATIVE

*Emphasizes low-latency systems techniques, lock-free queues, kernel event loops, and hardware acceleration.*

- **Engineered a low-latency electronic matching engine in C++17**: Implemented continuous limit order book matching using direct vector price ladders and 64-bit word bitmaps, finding best bid/ask levels in a single CPU instruction (`lzcnt`/`tzcnt`).
- **Designed an event-driven network gateway and binary wire protocol**: Built a non-blocking BSD `kqueue` TCP reactor processing compact big-endian binary frames with bounded socket backpressure handling and deterministic connection teardown.
- **Decoupled execution via lock-free SPSC queues**: Connected network ingestion, single-threaded matching, and event projection planes using cacheline-padded (`alignas(64)`) circular ring buffers, eliminating mutex contention and false sharing across CPU cores.
- **Rigorous verification across sanitizers and 5M-order simulation**: Verified shutdown drain linearization and lifecycle state machine invariants across 142 automated tests, clean under ASan, UBSan, and TSan (31.5s runtime).

---

## Interview Defense Cheat Sheet for Resume Bullets

| If the Interviewer Asks... | What They Want to Know | Your One-Sentence Defense |
| :--- | :--- | :--- |
| *"How did you achieve zero heap allocations on the hot path?"* | Do you understand memory allocators? | "All orders, price levels, and bitmaps are pre-allocated in pools and flat arrays at startup; orders are recycled via a vector free-list." |
| *"Why is cancellation strict O(1)?"* | Do you know intrusive pointers? | "Each order struct contains embedded `prev`/`next` pointers and is indexed in a pointer table (`orderLookup[id]`), allowing unlinking without searching the book." |
| *"Why use a single-threaded matching core?"* | Do you understand lock contention? | "Order matching is sequential; a single matching thread has zero lock contention, zero context switching, and keeps book data hot in L1/L2 CPU cache." |
| *"What is CQRS in this project?"* | Do you understand read/write separation? | "The matching core only handles matching and emits events; a background worker updates a separate in-memory Read Model that serves HTTP queries without blocking the engine." |
| *"How did you verify thread safety?"* | Do you know how to test concurrency? | "We ran 66 multithreaded test cases under Clang ThreadSanitizer (TSan) with zero data races, alongside a 5-million order multi-threaded simulation." |
