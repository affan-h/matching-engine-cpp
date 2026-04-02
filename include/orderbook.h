#pragma once

#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include <bitset>
#include "types.h"
#include "price_level.h"

class OrderBook {
private:
    static const int MAX_PRICE = 100000;

    std::vector<PriceLevel> bidLevels;
    std::vector<PriceLevel> askLevels;

    std::bitset<MAX_PRICE + 1> bidBitmap;
    std::bitset<MAX_PRICE + 1> askBitmap;
    
    int bestBid;
    int bestAsk;

    std::unordered_map<OrderId, std::list<Order>::iterator> orderLookup;

public:
    OrderBook();

    // Book state
    bool hasBids() const;
    bool hasAsks() const;

    int getBestBid() const;
    int getBestAsk() const;

    Order& getBestBidOrder();
    Order& getBestAskOrder();

    // Insert operations
    void insertBid(const Order& order);
    void insertAsk(const Order& order);

    // Remove best order (after fill)
    void removeBestBid();
    void removeBestAsk();

    // Order management
    bool cancelOrder(OrderId orderId);
    bool getOrder(OrderId id, Order& outOrder);

    void printBook() const;
};