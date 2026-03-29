#pragma once

#include <unordered_map>
#include <string>
#include "orderbook.h"

class MatchingEngine
{
private:

    std::unordered_map<std::string, OrderBook> books;

    Timestamp currentTimestamp = 0;
    OrderId nextOrderId = 100000;
    uint64_t nextTradeId = 0;

public:

    void addLimitOrder(
        const std::string& symbol,
        OrderId id,
        Side side,
        Price price,
        Quantity qty
    );

    void addMarketOrder(
        const std::string& symbol,
        Side side,
        Quantity qty
    );

    bool cancelOrder(
        const std::string& symbol,
        OrderId id
    );

    void modifyOrder(
        const std::string& symbol,
        OrderId id,
        Price newPrice,
        Quantity newQty
    );

    void printBook(const std::string& symbol) const;

    Trade createTrade(
        const std::string& symbol,
        const Order& incoming,
        const Order& resting,
        Quantity qty
    );

    void reportTrade(const Trade& trade);

    OrderId generateOrderId();
};