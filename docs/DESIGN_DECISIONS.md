# Architecture & Design Decisions

This document summarizes the major technical design decisions made in the project, why each was chosen, the alternative approaches considered, and the concrete technical reasons the alternatives were rejected.

---

## Design Decisions Matrix

| Decision | Why Chosen | Alternative Considered | Why Alternative Was Rejected |
| :--- | :--- | :--- | :--- |
| **C++ Implementation Language** | Predictable sub-microsecond latency, manual control over memory layout, zero garbage collection pauses, and direct access to CPU hardware bit-scan instructions. | Java, Go, or Rust | Java/Go garbage collection pauses introduce tail-latency spikes (p99/p99.9). Rust is an excellent choice, but C++ remains the standard in institutional electronic trading and systems infrastructure. |
| **Single-Threaded Matching Core** | Order matching for an instrument is inherently sequential. A single thread completely eliminates lock contention, mutex overhead, cache invalidation traffic, and race conditions. | Multi-threaded matching core with mutexes or fine-grained locks | Fine-grained locks or shared mutexes under high write contention cause thread context switching and cache bouncing that reduce throughput by 5x–10x compared to an un-contended single thread. |
| **Direct Vector Indexing for Price Levels** | Provides true $O(1)$ constant-time access to any price level via array offset (`bids[price]`), ensuring deterministic latency without hashing or tree traversals. | `std::map<Price, PriceLevel>` (Red-Black Tree) | Red-Black trees require $O(\log N)$ time, pointer chasing that causes cache misses, and dynamic heap node allocation during order insertion. |
| **64-Bit Word Bitmaps for Best Bid/Ask** | Hardware-accelerated bit-scan instructions (`__builtin_ctzll` / `__builtin_clzll`) find the next active price level in 1–2 CPU clock cycles. | Linear scan across price ladder, or tree traversal | Scanning 100,000 price levels linearly is $O(N)$ and wastes hundreds of cycles. Tree traversal is $O(\log N)$ with cache miss penalties. Bitmaps compress 100,000 levels into just 1,563 64-bit words. |
| **Intrusive Doubly Linked Lists for Orders** | Embedding `prev` and `next` pointers directly inside the `Order` struct allows order cancellation in strict $O(1)$ constant time with zero dynamic memory allocation. | `std::list<Order>` or `std::vector<Order>` | `std::list` allocates a separate heap node per order, destroying cache locality. `std::vector` requires $O(N)$ shifting upon cancellation in the middle of a price level. |
| **Pre-Allocated Order Pool** | Eliminates heap allocations (`malloc`/`new`) on the critical path. All orders are recycled through a vector-backed free-list. | Dynamic allocation via `new Order(...)` | Heap allocations invoke kernel memory allocators and take locks under concurrent multi-threading, introducing unpredictable microsecond latency spikes. |
| **CQRS Plane Separation (Command vs Query)** | Isolates the single-threaded matching core from read-heavy client queries (L2 book depth, trade history, order status) so queries never pause trade execution. | Unified data model where API reads directly from the active order book | Querying market depth would require locking the active order book, blocking incoming orders and increasing trading latency during periods of high market interest. |
| **Lock-Free SPSC Ring Buffer** | Decouples networking threads from the matching thread with wait-free $O(1)$ push/pop operations using acquire-release atomics and 64-byte cacheline isolation. | Mutex-protected `std::queue` or Channel | Mutex-protected queues cause thread contention and kernel sleep/wake transitions. SPSC queues allow producer and consumer to operate simultaneously without locking. |
| **Binary Big-Endian TCP Wire Protocol** | Fixed-size binary frames (17 to 25 bytes) minimize network packet size, eliminate JSON serialization overhead, and allow deterministic parsing in zero allocations. | JSON or XML over HTTP directly to the core | Text parsing requires string manipulation, memory allocations, and high CPU parsing overhead unacceptable for high-throughput messaging. |
| **In-Memory Read Model with `std::shared_mutex`** | Allows thousands of concurrent reader threads to inspect market depth and recent trades simultaneously using shared read locks (`std::shared_lock`), while the single Projector thread applies execution updates using an exclusive write lock. | Relational database (e.g. SQLite / PostgreSQL) for reads | A disk-backed database introduces disk I/O and serialization overhead (milliseconds vs sub-microseconds), which is incompatible with real-time trading dashboards. |
| **In-Memory Pure Architecture (No WAL)** | Keeps the architecture clean, focused, and deterministic for an educational exchange simulation without disk I/O dependencies. | Persistent Write-Ahead Logging (WAL) to disk | Adding a disk WAL introduces fsync latency tuning, disk failure modes, and recovery replay complexity that distracts from the core matching algorithms. Documented as a conscious design choice. |

---

## Quick Summary for Interviewers

> "Every design decision in this engine traces back to three core principles:
> 1. **Zero heap allocation on the hot path** (pre-allocated pools, direct vectors, intrusive lists).
> 2. **Eliminating lock contention** (single-threaded core, lock-free SPSC queues, CQRS separation).
> 3. **Hardware alignment** (cache-line padding, bit-scan instructions, L1/L2 cache friendliness)."
