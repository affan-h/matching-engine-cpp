#pragma once

#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include "types.h"
#include "price_level.h"

class OrderBook {
private:
    // Bids: highest price first
    std::map<Price, PriceLevel, std::greater<Price>> bids;

    // Asks: lowest price first
    std::map<Price, PriceLevel> asks;

    // Order lookup: id -> iterator
    std::unordered_map<OrderId, std::list<Order>::iterator> orderLookup;

    // std::vector<ExecutionEvent> events;

public:
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