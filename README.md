# Limit Order Book Matching Engine (C++)

This project implements a simplified exchange matching engine using price-time priority.

## Features

- Limit orders
- Market orders
- Order cancellation
- Order modification
- FIFO priority within price levels
- Multi-symbol exchange layer
- Execution event generation

## Architecture

Limit Order Book Matching Engine (C++)

                 Client / Simulator
                        │
                        ▼
                    Exchange
        ┌─────────────────────────────┐
        │ symbol → OrderBook          │
        │                             │
        │   AAPL → OrderBook          │
        │   TSLA → OrderBook          │
        │   BTCUSD → OrderBook        │
        └─────────────────────────────┘
                        │
                        ▼
                  OrderBook Engine
        ┌─────────────────────────────┐
        │ bids: price → FIFO orders   │
        │ asks: price → FIFO orders   │
        │ orderLookup: hash table     │
        │ execution events            │
        └─────────────────────────────┘
                        │
                        ▼
                   Matching Engine
             (price-time priority)

## Design Decisions

### Price-Time Priority
Orders are matched using strict price-time priority:
- Better prices execute first
- Orders at the same price execute FIFO

### Data Structures

std::map  
Used to maintain sorted price levels for bids and asks.

std::list  
Maintains FIFO ordering of orders within each price level.

std::unordered_map  
Provides O(1) lookup for order cancellation using stored iterators.

### Deterministic Matching

The matching engine processes orders sequentially to ensure deterministic execution and fairness.

## Complexity

Insert Order: O(log P)  
Cancel Order: O(1)  
Best bid/ask: O(1)
Matching: O(K)

P = number of price levels  
K = orders consumed during matching

## Future Improvements

- Replace `std::map` with price ladder
- Lock-free order queues
- Market data feed generation
- Persistence layer
- Multi-threaded symbol processing

## Example Output

TRADE 5 @ 105  
TRADE 5 @ 106  
UNFILLED MARKET 2

## Simulation Test

A random order generator simulates market activity.

Example run:

Final Order Book

--- ORDER BOOK ---
ASKS:
100 : 16
103 : 92
104 : 310
105 : 378
BIDS:
99 : 9
98 : 13
97 : 314
96 : 529
95 : 349

## Performance Benchmark

Processed 100000 orders in 137 ms
