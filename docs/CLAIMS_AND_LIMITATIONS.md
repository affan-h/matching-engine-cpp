# What NOT to Claim: Technical Accuracy & Interview Defense

*This document lists statements that sound impressive but are technically inaccurate or overclaimed. Use the accurate, interview-defensible phrasing provided below.*

---

## The "Do Not Say" vs "Say This Instead" Table

| ❌ DO NOT SAY | Why It Is Misleading | ✅ SAY THIS INSTEAD |
| :--- | :--- | :--- |
| **"Production exchange"** | A real financial exchange requires regulatory reporting, clearing house integration, persistent WAL, disaster recovery, and FIX/ITCH protocols. | **"An in-memory electronic order matching service modeled after institutional exchange architectures."** |
| **"Zero latency"** | Nothing has zero latency. Physics, CPU caches, and OS scheduling always introduce measurable elapsed time. | **"Measured single-digit microsecond benchmark latency (e.g. ~5.5 µs order insert, ~4.1 µs match)."** |
| **"Fully distributed system"** | The service runs as a single-process server with internal thread boundaries, not a multi-node distributed cluster. | **"A high-throughput single-node backend service utilizing thread boundary isolation."** |
| **"Durable / Persistent"** | The engine does not write transactions to a disk Write-Ahead Log (WAL) or database. State resets on process restart. | **"A pure in-memory architecture optimized for throughput and predictable latency without disk I/O bottlenecks."** |
| **"Linux production-ready"** | The network gateway uses BSD `kqueue`, which is specific to macOS and BSD systems. | **"Built on macOS using BSD `kqueue`; deploying to Linux would require an `epoll` reactor adapter."** |
| **"Completely lock-free architecture"** | While the command queue and matching core are lock-free, the Read Model uses `std::shared_mutex` for concurrent reader-writer access. | **"Lock-free single-producer single-consumer queues between networking and matching, with reader-writer locks on the read model."** |
| **"Automatic duplicate suppression"** | The matching engine generates a new monotonic `order_id` for every submitted order; duplicate `client_order_id` submissions are not rejected at the gateway. | **"`client_order_id` is a queryable correlation token enabling clients to check order status before issuing retries."** |
| **"Arbitrary price precision"** | The price ladder uses direct vector indexing, meaning prices must be discrete positive integers between 1 and 100,000. | **"Direct-indexed discrete price space (1 to 100,000 integer ticks), trading off arbitrary float ranges for deterministic $O(1)$ memory lookups."** |
| **"Multi-threaded matching engine"** | The matching core itself is strictly single-threaded; multi-threading exists in the gateway, projector, and REST layers. | **"A single-threaded matching core decoupled from concurrent network and query planes via lock-free ring buffers."** |

---

## Key Interview Defense Rules

1. **Be Proud of the Single-Threaded Core**:
   Don't apologize for having a single matching thread. In high-frequency trading and low-latency systems (e.g. LMAX Disruptor), a single thread with zero locks is *faster* than a multi-threaded core with mutexes.
2. **Be Honest About Persistence**:
   If asked *"What happens if the power cuts out?"*, answer immediately:
   > "In this prototype, in-memory state is lost because there is no persistent disk WAL. In a production system, I would add an append-only WAL using memory-mapped files (`mmap`) with asynchronous flushing to disk to replay state upon restart."
3. **Be Clear About Platform Scope**:
   If asked *"Can I run this on Ubuntu?"*, answer:
   > "The matching engine core and wire protocol are standard C++17 and compile anywhere. However, the network gateway uses BSD `kqueue` on macOS. To run on Linux, I would swap `kqueue` for `epoll` using a common event demultiplexer interface."
