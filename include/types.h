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

    Price price;
    Quantity quantity;

    Timestamp timestamp;
    std::uint64_t tradeId; 
};

// enum class EventType {
//     TRADE,
//     UNFILLED_MARKET
// };

// struct ExecutionEvent {
//     EventType type;
//     int price;
//     int quantity;

//     ExecutionEvent(EventType t, int p, int q)
//         : type(t), price(p), quantity(q) {}
// };

// struct Order {
//     int id;
//     bool isBuy;     // true = buy, false = sell
//     int price;
//     int quantity;

//     Order(int id_, bool isBuy_, int price_, int quantity_)
//         : id(id_), isBuy(isBuy_), price(price_), quantity(quantity_) {}
// };

// struct PriceLevel {
//     int price;
//     int totalVolume;
//     list<Order> orders;

//     PriceLevel(int p) : price(p), totalVolume(0) {}
// };