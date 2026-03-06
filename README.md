# Limit Order Book Matching Engine (C++)

A C++ implementation of a price–time priority limit order book and matching engine similar to those used in modern electronic trading systems.

## Features

- Limit orders
- Market orders
- Order cancellation
- Order modification
- FIFO priority within price levels
- Multi-symbol exchange layer
- Execution event generation

## Architecture Overview

The system is organized into three main layers:

```
Client / Simulator
        │
        ▼
     Exchange
        │
        ├── symbol → OrderBook
        │
        ▼
    OrderBook Engine
        │
        ├── bids: price → FIFO order queue
        ├── asks: price → FIFO order queue
        ├── orderLookup: hash table for O(1) cancellation
        └── execution events
        │
        ▼
     Matching Engine
 (price-time priority)
```

### Exchange Layer
The `Exchange` class manages multiple symbols and routes incoming orders to the appropriate `OrderBook`.

Example:

```
Exchange
   ├── AAPL → OrderBook
   ├── TSLA → OrderBook
   └── BTCUSD → OrderBook
```

This allows each market to operate independently and enables easy scaling.

### OrderBook
Each `OrderBook` maintains two sides of the market:

- **Bids** (buy orders) sorted from highest price to lowest
- **Asks** (sell orders) sorted from lowest price to highest

Orders at the same price level are stored in **FIFO order** to preserve time priority.

### Matching Engine
Incoming orders are processed sequentially.

Matching follows **price–time priority**:

1. Better prices execute first
2. Orders at the same price execute in FIFO order

Partial fills are supported, and completed trades generate execution events.

## Core Data Structures

| Structure | Purpose |
|----------|--------|
`std::map` | Maintain sorted price levels |
`std::list` | Preserve FIFO order within a price level |
`std::unordered_map` | O(1) lookup for order cancellation |
`std::vector` | Store execution events |

## Complexity

Insert Order: O(log P)  
Cancel Order: O(1)  
Best bid/ask: O(1)
Matching: O(K)

P = number of price levels  
K = orders consumed during matching

## Future Improvements

- Replace std::map with a price ladder for faster access
- Maintain aggregated volume at each price level
- Separate matching engine from order book
- Add persistence and market data feeds

## Example Output

TRADE 5 @ 105  
TRADE 5 @ 106  
UNFILLED MARKET 2

## Simulation Test

A random order generator simulates market activity.

Example run:

Final Order Book

```
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
```

## Performance Benchmark

Processed 100000 orders in 137 ms
