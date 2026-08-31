#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <functional>
#include "orderbook.h"
#include "symbol_registry.h"
#include "market_data.h"
#include "stats_tracker.h"
#include "outbound_events.h"
#include "spsc_queue.h"

class MatchingEngine {
private:
    std::vector<OrderBook> books;
    SymbolRegistry         registry;
    MarketDataFeed         feed;
    OrderId  nextOrderId = 0;
    uint64_t nextTradeId = 0;
    uint64_t totalTrades = 0;
    uint64_t totalOrdersAccepted = 0;
    uint64_t totalOrdersRejected = 0;
    uint64_t totalOrdersCancelled = 0;
    uint64_t totalVolume = 0;
    uint64_t l2CoalescedDrops = 0;
    uint64_t criticalEventRetries = 0;
    uint64_t criticalEventDrops = 0;

    OutboundEventQueue*    outboundQueue = nullptr;
    uint64_t               globalSequence = 0;

    void publishSnapshot(InstrumentId instrument);

    std::vector<std::function<void(InstrumentId, const std::string&,
                                   Price, Quantity, Side)>> tradeSubscribers;

    void publishOutbound(const events::OutboundEvent& event);
    void emitOrderState(
        OrderId id,
        uint64_t client_order_id,
        InstrumentId inst,
        Side side,
        Price price,
        Quantity orig_qty,
        Quantity rem_qty,
        Quantity filled_qty,
        events::OrderStatus status,
        events::RejectCode reject_code,
        Timestamp ts
    );

public:
    void setOutboundQueue(OutboundEventQueue* queue) {
        outboundQueue = queue;
    }

    uint64_t getLastSequence() const {
        return globalSequence;
    }

    // Allow external code to subscribe to market data
    void subscribeMarketData(SnapshotCallback cb) {
        feed.subscribe(std::move(cb));
    }

    void subscribeTradeData(
        std::function<void(InstrumentId, const std::string&,
                           Price, Quantity, Side)> cb)
    {
        tradeSubscribers.push_back(std::move(cb));
    }
    
    bool symbolExists(const std::string& symbol) const {
        return registry.exists(symbol);
    }

    // Instrument registration
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

    bool getOrder(InstrumentId inst, OrderId id, Order& out) {
        if (inst >= books.size()) return false;
        return books[inst].getOrder(id, out);
    }

    void refreshSnapshot(InstrumentId inst) {
        if (inst >= books.size()) return;
        std::vector<PriceLevelSnapshot> bids, asks;
        books[inst].getDepth(bids, asks, 10);
        feed.publishSnapshot(inst, registry.getSymbol(inst), bids, asks);
    }

    OrderId addLimitOrder(
        InstrumentId instrument,
        Side side,
        Price price,
        Quantity qty,
        TimeInForce tif = TimeInForce::GTC,
        uint64_t client_order_id = 0
    );

    OrderId addMarketOrder(
        InstrumentId instrument,
        Side side,
        Quantity qty,
        uint64_t client_order_id = 0
    );

    bool cancelOrder(
        InstrumentId instrument,
        OrderId id,
        uint64_t client_order_id = 0
    );

    bool modifyOrder(
        InstrumentId instrument,
        OrderId id,
        Price newPrice,
        Quantity newQty,
        uint64_t client_order_id = 0
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
    uint64_t getTotalOrdersAccepted() const { return totalOrdersAccepted; }
    uint64_t getTotalOrdersRejected() const { return totalOrdersRejected; }
    uint64_t getTotalOrdersCancelled() const { return totalOrdersCancelled; }
    uint64_t getTotalVolume() const { return totalVolume; }
    uint64_t getL2CoalescedDrops() const { return l2CoalescedDrops; }
    uint64_t getCriticalEventRetries() const { return criticalEventRetries; }
    uint64_t getCriticalEventDrops() const { return criticalEventDrops; }

    OrderId generateOrderId();
};
