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