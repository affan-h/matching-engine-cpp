#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>
#include <shared_mutex>
#include <mutex>
#include "types.h"
#include "outbound_events.h"

// ─────────────────────────────────────────────
// Read Model Query Structures
// ─────────────────────────────────────────────

struct L2BookState {
    InstrumentId instrument_id{0};
    std::string symbol;
    uint64_t sequence{0};
    Timestamp timestamp{0};
    uint8_t bid_count{0};
    uint8_t ask_count{0};
    events::PriceLevelRecord bids[events::MAX_L2_DEPTH]{};
    events::PriceLevelRecord asks[events::MAX_L2_DEPTH]{};
};

struct TradeRecord {
    uint64_t trade_id{0};
    InstrumentId instrument_id{0};
    std::string symbol;
    OrderId buy_order_id{0};
    OrderId sell_order_id{0};
    Side aggressor_side{Side::Buy};
    Price price{0};
    Quantity quantity{0};
    Timestamp timestamp{0};
    uint64_t sequence{0};
};

struct OrderRecord {
    OrderId order_id{0};
    uint64_t client_order_id{0};
    InstrumentId instrument_id{0};
    std::string symbol;
    Side side{Side::Buy};
    Price price{0};
    Quantity original_qty{0};
    Quantity remaining_qty{0};
    Quantity filled_qty{0};
    events::OrderStatus status{events::OrderStatus::New};
    events::RejectCode reject_code{events::RejectCode::None};
    Timestamp timestamp{0};
    uint64_t sequence{0};
};

struct EngineMetrics {
    uint64_t total_trades{0};
    uint64_t total_volume{0};
    uint64_t total_orders_accepted{0};
    uint64_t total_orders_filled{0};
    uint64_t total_orders_cancelled{0};
    uint64_t total_orders_rejected{0};
    uint64_t last_sequence{0};
    size_t   tracked_orders_count{0};
    size_t   registered_symbols_count{0};
};

// ─────────────────────────────────────────────
// Bounded Circular Buffer for Trade History
// ─────────────────────────────────────────────

class BoundedTradeHistory {
private:
    std::vector<TradeRecord> buffer;
    size_t capacity{1000};
    size_t head{0};
    size_t count{0};

public:
    explicit BoundedTradeHistory(size_t cap = 1000)
        : buffer(cap == 0 ? 1 : cap), capacity(cap == 0 ? 1 : cap), head(0), count(0) {}

    void add(const TradeRecord& trade) {
        buffer[head] = trade;
        head = (head + 1) % capacity;
        if (count < capacity) {
            count++;
        }
    }

    void getRecent(size_t limit, std::vector<TradeRecord>& out) const {
        size_t n = std::min(limit, count);
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            size_t idx = (head + capacity - 1 - i) % capacity;
            out.push_back(buffer[idx]);
        }
    }

    size_t size() const { return count; }
    size_t getCapacity() const { return capacity; }
};

// ─────────────────────────────────────────────
// Read Model (Query Plane)
// ─────────────────────────────────────────────

class ReadModel {
private:
    mutable std::shared_mutex rw_mutex;

    // Per-instrument latest L2 order book
    std::unordered_map<InstrumentId, L2BookState> l2_books;

    // Per-instrument bounded trade history
    std::unordered_map<InstrumentId, BoundedTradeHistory> trade_histories;

    // Bounded order state history (FIFO eviction)
    std::unordered_map<OrderId, OrderRecord> orders;
    std::unordered_map<uint64_t, OrderId> client_to_order_id;
    std::deque<OrderId> order_eviction_queue;
    size_t max_orders{10000};
    size_t max_trades_per_symbol{1000};

    // Symbol registry
    std::unordered_map<InstrumentId, std::string> id_to_symbol;
    std::unordered_map<std::string, InstrumentId> symbol_to_id;

    // Aggregate telemetry metrics
    EngineMetrics metrics;
    std::atomic<bool> is_synchronized{true};

    void recordOrderInternal(const OrderRecord& record);

public:
    explicit ReadModel(size_t max_orders_cap = 10000, size_t max_trades_cap = 1000);

    // Register symbols for name mapping
    void registerSymbol(InstrumentId id, const std::string& symbol);
    bool getInstrumentId(const std::string& symbol, InstrumentId& out) const;
    std::string getSymbol(InstrumentId id) const;

    // Projector Write API (Called ONLY by Projector thread)
    void applyEvent(const events::OutboundEvent& event);

    // Concurrent Query API (Protected by shared_mutex)
    bool getL2Book(InstrumentId id, L2BookState& out) const;
    bool getL2BookBySymbol(const std::string& symbol, L2BookState& out) const;

    bool getRecentTrades(InstrumentId id, size_t limit, std::vector<TradeRecord>& out) const;
    bool getRecentTradesBySymbol(const std::string& symbol, size_t limit, std::vector<TradeRecord>& out) const;

    bool getOrder(OrderId id, OrderRecord& out) const;
    bool getOrderByClientId(uint64_t client_order_id, OrderRecord& out) const;

    bool getMetrics(EngineMetrics& out) const;
    uint64_t getLastSequence() const;

    // Synchronization and Readiness
    bool isReady() const { return is_synchronized.load(std::memory_order_relaxed); }
    void setSynchronized(bool syncd) { is_synchronized.store(syncd, std::memory_order_release); }

    void getRegisteredSymbols(std::vector<std::pair<InstrumentId, std::string>>& out) const;
    size_t getTradeCount(InstrumentId id) const;
    size_t getOrderCount() const;
};
