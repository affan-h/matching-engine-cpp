# Spoken Demo Guide (2–3 Minutes)

This script provides the exact command, terminal output, under-the-hood explanation, and conversational spoken text for each step of the live demo.

---

## Pre-Demo Run Command

Run the automated demo in your terminal:
```bash
./scripts/demo.sh
```

---

## Step-by-Step Script

### Step 0: Starting the Demo
- **COMMAND**: `./scripts/demo.sh`
- **WHAT THE USER SEES**: Colored banner: `Matching Engine: 2-Minute End-to-End Architecture Demo` and background service startup on dynamic ports.
- **WHAT IS HAPPENING**: The script compiles the C++ binary if needed, discovers two unused ephemeral ports, starts `./gateway` and `uvicorn api.main:app`, and sets up clean process exit traps.
- **WHAT I SAY**:
  > "I built an in-memory order matching service in C++ with a Python FastAPI REST interface. To show how orders flow end-to-end, I wrote a demo script that starts the C++ engine and REST API, places orders, executes a trade, cancels a resting order, and verifies an atomic Fill-or-Kill rejection."

---

### Step 1: System Health Check
- **COMMAND**: `GET /health`
- **WHAT THE USER SEES**:
  ```json
  {
      "status": "healthy",
      "ready": true,
      "gateway": {"host": "127.0.0.1", "connected": true, "rtt_ms": 0.4},
      "read_model": {"active": true, "registered_symbols": 4}
  }
  ```
- **WHAT IS HAPPENING**: FastAPI connects to the C++ gateway, performs an 8-byte binary ping/pong to verify TCP liveness and RTT, and confirms the in-memory read model is ready for queries.
- **WHAT I SAY**:
  > "First, we verify system health. The REST server performs a quick binary ping-pong with the C++ engine to measure round-trip latency—here about 0.4 milliseconds—and checks that our read model is ready to serve queries."

---

### Step 2 & 3: Place a Resting BUY Order & Inspect the Book
- **COMMAND**:
  - `POST /orders` $\rightarrow$ `{"symbol":"AAPL","side":"buy","order_type":"limit","price":150,"quantity":100}`
  - `GET /book/AAPL`
- **WHAT THE USER SEES**:
  - Order response: `status: ACCEPTED`
  - Order book response:
    ```json
    {
        "symbol": "AAPL",
        "bids": [{"price": 150, "quantity": 100}],
        "asks": []
    }
    ```
- **WHAT IS HAPPENING**: FastAPI accepts the request and forwards a binary frame to the C++ engine. Since there are currently no sellers, the order rests at price level $150 on the bid side.
- **WHAT I SAY**:
  > "Now, a buyer submits an order for 100 shares of Apple at $150. The REST API validates the input and forwards it to the C++ engine. Since there are no sellers yet, the order rests in the book. Querying the order book shows 100 shares waiting on the bid side."

---

### Step 4, 5 & 6: Submit a Matching SELL Order & Verify Trade
- **COMMAND**:
  - `POST /orders` $\rightarrow$ `{"symbol":"AAPL","side":"sell","order_type":"limit","price":150,"quantity":40}`
  - `GET /trades/AAPL`
  - `GET /book/AAPL`
- **WHAT THE USER SEES**:
  - Trade log:
    ```json
    [{"trade_id": 1, "price": 150, "quantity": 40, "aggressor_side": "sell"}]
    ```
  - Order book:
    ```json
    {"bids": [{"price": 150, "quantity": 60}], "asks": []}
    ```
- **WHAT IS HAPPENING**: A seller submits 40 shares at $150. The engine matches it immediately with the resting buyer at $150. A 40-share trade is executed, and the resting buyer's remaining shares drop from 100 to 60.
- **WHAT I SAY**:
  > "Next, a seller submits 40 shares at $150. The engine matches the prices, executes a 40-share trade immediately, and decrements the resting buyer's shares. If we check the trade log, we see the trade, and the order book confirms the bid has decremented from 100 down to 60 shares."

---

### Step 7 & 8: Place a Resting SELL Order to Create a Two-Sided Market
- **COMMAND**:
  - `POST /orders` $\rightarrow$ `{"symbol":"AAPL","side":"sell","order_type":"limit","price":155,"quantity":50}`
  - `GET /book/AAPL`
- **WHAT THE USER SEES**:
  ```json
  {
      "bids": [{"price": 150, "quantity": 60}],
      "asks": [{"price": 155, "quantity": 50}]
  }
  ```
- **WHAT IS HAPPENING**: The seller's limit price ($155) is higher than the best bid ($150), so no match occurs. It rests on the ask side, forming a two-sided market with a $5 spread.
- **WHAT I SAY**:
  > "Now we submit a sell order for 50 shares at $155. Because the asking price is higher than the current bid of $150, there is no match, and it rests on the book. Querying the book now shows both sides: buyers at $150, sellers at $155."

---

### Step 9 & 10: Cancel the Resting Order
- **COMMAND**:
  - `DELETE /orders/3?symbol=AAPL`
  - `GET /book/AAPL`
- **WHAT THE USER SEES**:
  - Cancel response: `status: ACCEPTED`
  - Order book: `asks: []` (ask side is empty again).
- **WHAT IS HAPPENING**: The engine looks up Order 3 directly in an index table and unlinks it from the $155 price level in constant $O(1)$ time without searching through other orders.
- **WHAT I SAY**:
  > "Now we cancel Order 3. Instead of searching through every order, the engine uses a direct index to find the order and unlinks it in O(1) constant time. Querying the book confirms the ask side is empty again."

---

### Step 11 & 12: Atomic Fill-or-Kill (FOK) Rejection
- **COMMAND**:
  - `POST /orders` $\rightarrow$ `{"symbol":"AAPL","side":"sell","price":150,"quantity":500,"time_in_force":"FOK"}`
  - `GET /orders/4`
- **WHAT THE USER SEES**:
  ```json
  {
      "order_id": 4,
      "original_quantity": 500,
      "remaining_quantity": 0,
      "filled_quantity": 0,
      "status": "REJECTED",
      "reject_code": "INSUFFICIENT_LIQUIDITY_FOK"
  }
  ```
- **WHAT IS HAPPENING**: The client specifies Fill-or-Kill for 500 shares, but the resting bid only has 60 shares available. The engine checks available volume upfront, rejects the entire order atomically, and executes 0 partial fills.
- **WHAT I SAY**:
  > "Finally, we demonstrate time-in-force atomicity with a Fill-or-Kill order. The client wants to sell 500 shares at $150, but our resting bid only has 60 shares. The engine checks available volume before modifying anything, rejects the entire order atomically, and leaves the book completely untouched."

---

## Wrap-Up Statement

- **WHAT I SAY**:
  > "That completes the walkthrough. The matching engine executes trades deterministically with zero heap allocations on the hot path, while keeping networking and queries on separate threads so heavy client traffic never blocks trade execution."
