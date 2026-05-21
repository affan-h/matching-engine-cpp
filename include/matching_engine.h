#pragma once

#include <unordered_map>
#include <string>
#include "orderbook.h"
#include "symbol_registry.h"
#include "market_data.h"
#include "stats_tracker.h"

class MatchingEngine {
private:
    std::vector<OrderBook> books;
    SymbolRegistry         registry;
    MarketDataFeed         feed;
    OrderId  nextOrderId = 0;
    uint64_t nextTradeId = 0;
    uint64_t totalTrades = 0;

    void publishSnapshot(InstrumentId instrument);

    std::vector<std::function<void(InstrumentId, const std::string&,
                                   Price, Quantity)>> tradeSubscribers;

public:
    // Allow external code to subscribe to market data
    void subscribeMarketData(SnapshotCallback cb) {
        feed.subscribe(std::move(cb));
    }

    void subscribeTradeData(
        std::function<void(InstrumentId, const std::string&,
                           Price, Quantity)> cb)
    {
        tradeSubscribers.push_back(std::move(cb));
    }

    // New convenience overloads that accept symbol strings
    InstrumentId registerInstrument(const std::string& symbol) {
        InstrumentId id = registry.registerSymbol(symbol);
        if (id >= books.size())
            books.resize(id + 1);
        return id;
    }

    InstrumentId getInstrumentId(const std::string& symbol) const {
        return registry.getId(symbol);
    }

    const std::string& getSymbol(InstrumentId id) const {
        return registry.getSymbol(id);
    }

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