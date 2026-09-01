# 2.5-Minute Demo Video Recording Plan

*A clean, structured screen-recording guide to showcase the project on GitHub or portfolio sites.*

---

## Screen Setup
- **Left Half of Screen**: Clean terminal window (120 columns wide, large readable monospace font).
- **Right Half of Screen**: Browser window displaying `README.md` or the ASCII architecture diagram.

---

## Timeline & Spoken Script

### 0:00 – 0:20 | Introduction: What the Project Is
- **Visual**: Show project repository root / `README.md` header.
- **Spoken**:
  > "Hi, this is a demonstration of an in-memory limit order matching service I built in C++ with a Python FastAPI REST interface. It implements fair price-time priority matching, deterministic $O(1)$ order cancellation, and decouples network I/O from core matching using lock-free ring buffers."

---

### 0:20 – 0:40 | High-Level Architecture
- **Visual**: Point cursor to the ASCII architecture diagram in `README.md`.
- **Spoken**:
  > "Architecturally, it follows a CQRS pattern. External clients submit orders through FastAPI, which forwards binary frames to a non-blocking C++ gateway. A lock-free SPSC queue feeds a single-threaded matching core with zero heap allocations on the hot path. Execution events are projected into an in-memory Read Model, allowing concurrent HTTP queries without contending with the matching engine."

---

### 0:40 – 1:50 | The Live Demo (Terminal Walkthrough)
- **Visual**: Switch focus to terminal and execute:
  ```bash
  ./scripts/demo.sh
  ```
- **Spoken (timed with script execution)**:
  - *(Step 1 Health)*: "The demo starts the C++ gateway and REST server, and verifies system health with a 0.4 millisecond binary ping/pong."
  - *(Step 2 & 3 Buy Order)*: "Next, we place an initial buy order for 100 shares of Apple at $150. Querying the book shows 100 shares resting on the bid."
  - *(Step 4, 5 & 6 Aggressive Sell & Match)*: "A seller submits 40 shares at $150. The engine matches immediately at the resting price, executes a 40-share trade, and decrements the resting bid from 100 down to 60 shares."
  - *(Step 7 & 8 Two-Sided Depth)*: "We place a resting ask at $155, creating a live two-sided market with a $5 spread."
  - *(Step 9 & 10 Cancellation)*: "We cancel the resting ask. Using intrusive pointers, the engine unlinks the order in strict O(1) constant time without scanning the book."
  - *(Step 11 & 12 FOK Rejection)*: "Finally, we submit an atomic Fill-or-Kill order for 500 shares. Since only 60 shares are available, the engine rejects the entire order with zero partial fills and zero book mutation."

---

### 1:50 – 2:20 | Key Engineering Decisions
- **Visual**: Point to `docs/DESIGN_DECISIONS.md` table.
- **Spoken**:
  > "Three key decisions make this performant: First, using a single-threaded core completely avoids mutex contention and cache bouncing. Second, direct vector price arrays and 64-bit word bitmaps allow finding the best bid or ask in a single CPU bit-scan instruction (`tzcnt`/`lzcnt`). Third, pre-allocating an order pool eliminates dynamic memory allocations on the matching path."

---

### 2:20 – 2:40 | Verification & Performance
- **Visual**: Terminal showing test counts or benchmark table in `README.md`.
- **Spoken**:
  > "The codebase is backed by 142 automated tests and runs completely clean under AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer with zero data races. Benchmarks show order matching at ~4.1 microseconds, and O(1) cancellation at ~3.5 microseconds—more than twice as fast as naive linked-list search under contention."

---

### 2:40 – 3:00 | Limitations & Future Work
- **Visual**: Return to terminal showing clean exit.
- **Spoken**:
  > "The system is currently pure in-memory and uses BSD kqueue on macOS. In a production setting, the natural next steps would be adding an append-only Write-Ahead Log for crash recovery and an epoll adapter for Linux deployments. Thanks for watching!"
