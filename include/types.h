#pragma once

#include <list>
#include <vector>
#include <cstdint>
#include <string>

using OrderId = std::uint64_t;
using Price   = int;
using Quantity = int;
using Timestamp = std::uint64_t;

enum class Side
{
    Buy,
    Sell
};

struct Order
{
    OrderId id;
    Side side;

    Price price;
    Quantity quantity;

    Timestamp timestamp;
};

struct Trade
{
    std::string symbol;

    OrderId buyOrderId;
    OrderId sellOrderId;

    Side aggressorSide;
    
    Price price;
    Quantity quantity;

    Timestamp timestamp;
    std::uint64_t tradeId; 
};