#pragma once

#include <unordered_map>
#include <string>
#include "orderbook.h"

class MatchingEngine
{
private:

    std::vector<OrderBook> books;
    OrderId nextOrderId = 0;
    uint64_t nextTradeId = 0;
    uint64_t totalTrades = 0;

public:

    OrderId addLimitOrder(
        InstrumentId instrument,
        Side side,
        Price price,
        Quantity qty,
        TimeInForce tif
    );

    OrderId addMarketOrder(
        InstrumentId instrument,
        Side side,
        Quantity qty
    );

    bool cancelOrder(
        InstrumentId instrument,
        OrderId id
    );

    bool modifyOrder(
        InstrumentId instrument,
        OrderId id,
        Price newPrice,
        Quantity newQty
    );

    void printOrderBook(InstrumentId instrument) const;

    Trade createTrade(
        InstrumentId instrument,
        const Order& incoming,
        const Order& resting,
        Quantity qty
    );

    void logTrade(const Trade& trade);

    uint64_t getTotalTrades() const { return totalTrades; }

    OrderId generateOrderId();
};