#pragma once

#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include "types.h"
#include "market_data.h"

struct InstrumentStats {
    std::string symbol;

    // Volume Weighted Average Price
    double   vwap          = 0.0;
    uint64_t totalVolume   = 0;
    uint64_t totalTurnover = 0;   // sum of (price * qty) for each trade

    // Spread tracking
    Price    lastSpread    = -1;
    double   avgSpread     = 0.0;
    uint64_t spreadSamples = 0;

    // Price range
    Price    highBid       = 0;
    Price    lowAsk        = INT_MAX;

    // Snapshot count (how many book updates seen)
    uint64_t updates       = 0;
};

class StatsTracker {
private:
    std::unordered_map<InstrumentId, InstrumentStats> stats;

public:
    // Called on each trade to update VWAP
    void recordTrade(InstrumentId id, const std::string& symbol,
                     Price price, Quantity qty)
    {
        auto& s = stats[id];
        s.symbol        = symbol;
        s.totalVolume   += qty;
        s.totalTurnover += static_cast<uint64_t>(price) * qty;
        s.vwap = s.totalVolume > 0
            ? static_cast<double>(s.totalTurnover) / s.totalVolume
            : 0.0;
    }

    // Called on each L2 snapshot to update spread/price stats
    void onSnapshot(const L2Snapshot& snap) {
        auto& s = stats[snap.instrument];
        s.symbol  = snap.symbol;
        s.updates++;

        // Spread tracking
        if (snap.spread() >= 0) {
            s.lastSpread = snap.spread();
            s.avgSpread  = (s.avgSpread * s.spreadSamples + snap.spread())
                           / (s.spreadSamples + 1);
            s.spreadSamples++;
        }

        // Price range
        if (!snap.bids.empty())
            s.highBid = std::max(s.highBid, snap.bids[0].price);
        if (!snap.asks.empty())
            s.lowAsk = std::min(s.lowAsk, snap.asks[0].price);
    }

    const InstrumentStats* getStats(InstrumentId id) const {
        auto it = stats.find(id);
        return it != stats.end() ? &it->second : nullptr;
    }

    void printAll() const {
        std::cout << "\n===== Market Statistics =====\n";
        for (auto& [id, s] : stats) {
            std::cout << "\n" << s.symbol << ":\n";
            std::cout << "  VWAP         : ";
            if (s.totalVolume > 0)
                std::cout << std::fixed
                          << std::setprecision(2) << s.vwap << "\n";
            else
                std::cout << "N/A\n";
            std::cout << "  Total volume : " << s.totalVolume   << "\n";
            std::cout << "  Book updates : " << s.updates       << "\n";
            std::cout << "  Avg spread   : ";
            if (s.spreadSamples > 0)
                std::cout << std::fixed
                          << std::setprecision(2) << s.avgSpread << "\n";
            else
                std::cout << "N/A\n";
            std::cout << "  High bid     : "
                      << (s.highBid > 0 ? std::to_string(s.highBid) : "N/A")
                      << "\n";
            std::cout << "  Low ask      : "
                      << (s.lowAsk < INT_MAX ? std::to_string(s.lowAsk) : "N/A")
                      << "\n";
        }
        std::cout << "=============================\n";
    }
};