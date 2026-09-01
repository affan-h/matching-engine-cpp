# Resume Project Descriptions & Bullets

Use the following tailored descriptions and bullet points on your resume. Every bullet point directly corresponds to concepts covered and defended in `docs/INTERVIEW_GUIDE.md`.

---

## Project Header

**High-Performance Electronic Order Matching Service (C++ / Python)**

### 1-Line Summary
> Designed and built an in-memory limit order matching engine in C++ exposed via a non-blocking TCP gateway and FastAPI REST interface, achieving sub-microsecond matching and $O(1)$ order cancellation.

---

## 3-Bullet Version (Compact / General Backend SDE)

- **Engineered an in-memory electronic matching engine in C++**: Implemented price-time priority (FIFO) matching with zero heap allocations on the hot path, using direct-indexed price ladders and hardware bit-scan instructions (`lzcnt`/`tzcnt`) for $O(1)$ best bid/ask lookups.
- **Architected a high-throughput CQRS concurrency model**: Decoupled network ingestion from core matching using non-blocking BSD `kqueue` sockets and cache-aligned lock-free SPSC queues, projecting monotonic execution events to an in-memory read model for concurrent HTTP queries.
- **Enforced production-grade reliability and test coverage**: Authored 142 automated tests, verified zero memory leaks or data races under ASan/UBSan and TSan, and demonstrated 100% volume conservation across a 5-million order multi-threaded simulation.

---

## 4-Bullet Version (Detailed / Systems & High-Throughput Focus)

- **Designed a low-latency C++ limit order book**: Built an intrusive doubly linked list and pre-allocated object pool achieving strict $O(1)$ order cancellation, demonstrating a 2.34x speedup (3.5 µs vs 8.3 µs) over naive list traversal under high book contention.
- **Built an event-driven network gateway and binary protocol**: Implemented a non-blocking BSD `kqueue` TCP reactor processing big-endian binary wire frames with bounded socket backpressure handling and deterministic connection teardown.
- **Decoupled execution via lock-free SPSC queues & CQRS**: Connected the single-threaded matching core to ingress and read planes using 64-byte cacheline-padded ring buffers, preventing lock contention and eliminating false sharing across CPU cores.
- **Rigorous verification across sanitizers and 5M-order simulation**: Verified complete shutdown drain linearization and lifecycle state machine invariants across 142 unit/integration tests, clean under Clang AddressSanitizer and ThreadSanitizer.

---

## Interview Defense Cheat Sheet for Resume Bullets

| Resume Bullet Phrase | Interviewer Question | Your One-Sentence Defense |
| :--- | :--- | :--- |
| *"Zero heap allocations on the hot path"* | *"How did you prevent allocations?"* | "All orders, price levels, and bitmaps are pre-allocated in pools and flat vectors at startup; orders are recycled via a vector free-list." |
| *"$O(1)$ order cancellation"* | *"Why is it O(1)?"* | "Orders use intrusive pointers inside pre-allocated nodes and are indexed in an array table, allowing pointer unlinking without searching the book." |
| *"Hardware bit-scan instructions"* | *"How does the bitmap work?"* | "Each bit in a 64-bit word represents an active price level; `ctzll` and `clzll` locate the lowest ask and highest bid in 1 CPU instruction." |
| *"Lock-free SPSC queues"* | *"Why SPSC and not MPSC?"* | "The TCP gateway multiplexes clients on a single kqueue reactor thread, so there is only 1 producer pushing and 1 engine thread popping." |
| *"Preventing false sharing"* | *"How do you isolate cache lines?"* | "We use `alignas(64)` on the atomic `head` and `tail` pointers so each variable resides on its own CPU L1 cache line." |
| *"ThreadSanitizer clean"* | *"Did you catch any concurrency bugs?"* | "Yes, TSan caught an unbounded traffic loop during shutdown that delayed drain; fixing it reduced TSan test runtime from 30 minutes to 2.4 seconds." |
