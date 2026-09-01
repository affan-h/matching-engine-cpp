# 2-Minute Interview Demo Script

This guide provides a step-by-step walkthrough of what to run, what happens under the hood, and exactly what to say to an interviewer during a 2–3 minute live demonstration.

---

## Preparation (Before the Interview)

Ensure the virtual environment is ready:
```bash
./scripts/demo.sh
```
*(The demo script automatically compiles the gateway if needed, finds open ephemeral ports, starts both services, runs the full scenario with pretty-printed JSON, and cleanly terminates background processes on exit).*

---

## Step-by-Step Demo Flow

### Step 0: Starting the Demo
**Action**: Run the demo script in your terminal:
```bash
./scripts/demo.sh
```

**What I Should Say**:
> "I built an in-memory electronic matching engine in C++ with a Python FastAPI REST interface. To show how orders flow end-to-end through the system, I wrote a deterministic script that starts the C++ gateway and REST API, places orders, executes a trade, cancels a resting order, and verifies an atomic Fill-or-Kill rejection."

---

### Step 1: Service Health & Round-Trip Ping
**Command**:
`GET /health`

**What Happens**:
FastAPI opens a TCP connection to the C++ gateway, sends an 8-byte binary `Ping` frame, receives an 8-byte `Pong` response, calculates the round-trip time (RTT ~0.4 ms), and checks that the in-memory Read Model is active and ready.

**What I Should Say**:
> "First, we check the health endpoint. The REST server performs a lightweight binary ping/pong with the C++ gateway over TCP to measure round-trip latency and verify that the background read model is ready to serve queries."

---

### Step 2 & 3: Placing a Resting Bid
**Command**:
`POST /orders` -> `{"symbol":"AAPL","side":"buy","price":150,"quantity":100}`
Followed by: `GET /book/AAPL`

**What Happens**:
1. FastAPI validates the payload and serializes it into an explicit 17-byte big-endian binary frame.
2. The C++ Gateway pushes the order into a lock-free Single-Producer Single-Consumer (SPSC) queue.
3. The single-threaded matching core checks the AAPL ask book. Since the book is empty, the order rests at price level \$150.
4. The engine emits an `OrderState` and `L2Update` event into an outbound queue, which the Projector worker applies to the in-memory Read Model.
5. `GET /book/AAPL` returns 100 shares at \$150 on the bid side.

**What I Should Say**:
> "Now, a client submits a limit order to buy 100 shares of Apple at \$150. The REST layer validates the request and forwards a compact binary frame over TCP to the C++ gateway. Since there are currently no sellers, the engine places the order on the book. When we query the order book depth, we see 100 shares resting on the bid."

---

### Step 4, 5 & 6: Aggressive Sell & Partial Match
**Command**:
`POST /orders` -> `{"symbol":"AAPL","side":"sell","price":150,"quantity":40}`
Followed by: `GET /trades/AAPL` and `GET /book/AAPL`

**What Happens**:
1. An incoming aggressive sell order for 40 shares at \$150 enters the engine.
2. The engine immediately finds our resting buy order at \$150.
3. A trade for 40 shares is executed at \$150.
4. The resting buy order's quantity decreases from 100 to 60 shares.
5. When querying `/trades/AAPL`, we see the executed trade with buyer order ID 1 and seller order ID 2.
6. When querying `/book/AAPL`, the resting bid has updated from 100 to 60 shares.

**What I Should Say**:
> "Next, another participant sends an aggressive sell order for 40 shares at \$150. The matching engine matches the price, generates an immediate trade for 40 shares, decrements the resting bid, and emits execution events. If we check the trade log, the trade is recorded, and the order book shows the bid level has automatically decremented from 100 down to 60 shares."

---

### Step 7 & 8: Two-Sided Market
**Command**:
`POST /orders` -> `{"symbol":"AAPL","side":"sell","price":155,"quantity":50}`
Followed by: `GET /book/AAPL`

**What Happens**:
1. A sell order at \$155 enters.
2. The best bid is \$150, so \$155 cannot match.
3. It rests on the ask side at \$155.
4. `GET /book/AAPL` now displays a two-sided market: Bid 60 @ \$150, Ask 50 @ \$155 (Spread: \$5).

**What I Should Say**:
> "Now we place a resting ask for 50 shares at \$155. Because the price is higher than the best bid of \$150, there is no cross, and the order rests. Querying the book now shows a two-sided market with a \$5 spread."

---

### Step 9 & 10: O(1) Cancellation
**Command**:
`DELETE /orders/3?symbol=AAPL`
Followed by: `GET /book/AAPL`

**What Happens**:
1. The engine looks up Order 3 directly in a pre-allocated index table ($O(1)$) and unlinks its intrusive doubly linked list node from the \$155 price level ($O(1)$).
2. The price level volume drops to 0, and the corresponding bit in the ask bitmap is cleared.
3. `GET /book/AAPL` confirms the ask side is empty again.

**What I Should Say**:
> "To demonstrate order cancellation, we delete Order 3. The engine uses a pre-allocated pointer table and intrusive doubly linked list nodes to unlink the order in strict O(1) constant time without searching the book. Querying the book confirms the ask level is cleanly removed."

---

### Step 11 & 12: Atomic Fill-or-Kill (FOK) Rejection
**Command**:
`POST /orders` -> `{"symbol":"AAPL","side":"sell","price":150,"quantity":500,"time_in_force":"FOK"}`
Followed by: `GET /orders/4`

**What Happens**:
1. The client wants 500 shares filled completely or not at all.
2. The available liquidity at \$150 is only 60 shares.
3. The engine checks available liquidity *before* mutating any order state.
4. Because 60 < 500, the engine atomically rejects the entire order with reject code `INSUFFICIENT_LIQUIDITY_FOK`. Zero trades execute and zero book state is altered.
5. `GET /orders/4` reports `status: REJECTED` with `reject_code: INSUFFICIENT_LIQUIDITY_FOK`.

**What I Should Say**:
> "Finally, we demonstrate time-in-force atomicity with a Fill-or-Kill order. The client requests to sell 500 shares at \$150, but our resting bid only has 60 shares. The engine inspects the price level before modifying any pointers, detects insufficient volume, and atomically rejects the entire order with zero partial fills and zero book corruption."

---

## Wrap-Up Summary

**What I Should Say to Conclude**:
> "This demonstrates the core CQRS design: a single-threaded C++ matching core executing trades with zero heap allocations on the hot path, communicating via lock-free ring buffers to a non-blocking TCP gateway, with an in-memory Read Model that allows concurrent HTTP clients to query live depth and trade history without taking locks on the matching engine."
