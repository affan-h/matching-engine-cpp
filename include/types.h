#pragma once

#include <list>
#include <vector>
#include <cstdint>
#include <string>

using OrderId = std::uint64_t;
using Price   = int;
using Quantity = int;
using Timestamp = std::uint64_t;
using InstrumentId = std::uint32_t;

enum class Side
{
    Buy,
    Sell
};

enum class TimeInForce : uint8_t
{
    GTC, // Good 'Til Canceled (Standard Limit)
    IOC, // Immediate Or Cancel
    FOK  // Fill Or Kill
};

struct Order
{
    OrderId id;
    Side side;

    Price price;
    Quantity quantity;

    Timestamp timestamp;
    TimeInForce tif;

    Order* prev = nullptr;
    Order* next = nullptr;
};

struct Trade
{
    InstrumentId instrument;

    OrderId buyOrderId;
    OrderId sellOrderId;

    Side aggressorSide;
    
    Price price;
    Quantity quantity;

    Timestamp timestamp;
    std::uint64_t tradeId; 
};

struct PriceLevelSnapshot {
    Price    price;
    Quantity volume;
    int      orderCount;
};

struct L2Snapshot {
    InstrumentId instrument;
    std::string  symbol;
    Timestamp    timestamp;

    std::vector<PriceLevelSnapshot> bids; // best bid first
    std::vector<PriceLevelSnapshot> asks; // best ask first

    // Derived fields
    Price bestBid()  const { return bids.empty() ? 0 : bids[0].price; }
    Price bestAsk()  const { return asks.empty() ? 0 : asks[0].price; }
    Price spread()   const {
        if (bids.empty() || asks.empty()) return -1;
        return asks[0].price - bids[0].price;
    }
    double midPrice() const {
        if (bids.empty() || asks.empty()) return 0.0;
        return (bids[0].price + asks[0].price) / 2.0;
    }
};