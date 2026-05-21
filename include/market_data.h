#pragma once
#include <functional>
#include <vector>
#include <chrono>
#include "types.h"

// Callback type: called every time a snapshot is published
using SnapshotCallback = std::function<void(const L2Snapshot&)>;

class MarketDataFeed {
private:
    std::vector<SnapshotCallback> subscribers;

public:
    // Register a subscriber (e.g. the CLI, a logger, a stats collector)
    void subscribe(SnapshotCallback cb) {
        subscribers.push_back(std::move(cb));
    }

    // Called by the engine after every order book change
    void publish(const L2Snapshot& snapshot) {
        for (auto& cb : subscribers)
            cb(snapshot);
    }

    // Build a snapshot and publish it
    void publishSnapshot(
        InstrumentId instrument,
        const std::string& symbol,
        const std::vector<PriceLevelSnapshot>& bids,
        const std::vector<PriceLevelSnapshot>& asks)
    {
        L2Snapshot snap;
        snap.instrument = instrument;
        snap.symbol     = symbol;
        snap.timestamp  = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        snap.bids = bids;
        snap.asks = asks;
        publish(snap);
    }
};