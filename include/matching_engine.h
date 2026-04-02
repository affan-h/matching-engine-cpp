#pragma once

#include <unordered_map>
#include <string>
#include "orderbook.h"

class MatchingEngine
{
private:

    std::unordered_map<std::string, OrderBook> books;

    //Timestamp currentTimestamp = 0;
    OrderId nextOrderId = 100000;
    uint64_t nextTradeId = 0;

public:

    OrderId addLimitOrder(
        const std::string& symbol,
        Side side,
        Price price,
        Quantity qty
    );

    OrderId addMarketOrder(
        const std::string& symbol,
        Side side,
        Quantity qty
    );

    bool cancelOrder(
        const std::string& symbol,
        OrderId id
    );

    bool modifyOrder(
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

    void logTrade(const Trade& trade);

    //void reportTrade(const Trade& trade);

    OrderId generateOrderId();
};