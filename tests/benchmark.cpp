#include <benchmark/benchmark.h>
#include "matching_engine.h"
#include <map>
#include <queue>

// -------------------------------------------------------
// NAIVE BASELINE: std::map + std::queue (the obvious impl)
// This is what most people would write first.
// We benchmark it to show what our optimizations actually buy.
// -------------------------------------------------------
struct NaiveOrder {
    uint64_t id;
    int price;
    int quantity;
};

class NaiveOrderBook {
    // map: price -> queue of orders (sorted, but heap-allocated nodes)
    std::map<int, std::queue<NaiveOrder>, std::greater<int>> bids;
    std::map<int, std::queue<NaiveOrder>>                    asks;
    uint64_t nextId = 1;

public:
    uint64_t insert(int side, int price, int qty) {
        uint64_t id = nextId++;
        NaiveOrder o{id, price, qty};
        if (side == 0) bids[price].push(o);
        else           asks[price].push(o);
        return id;
    }

    void matchBuy(int price, int qty) {
        while (qty > 0 && !asks.empty()) {
            auto it = asks.begin();
            if (it->first > price) break;
            auto& q = it->second;
            int take = std::min(qty, q.front().quantity);
            qty -= take;
            q.front().quantity -= take;
            if (q.front().quantity == 0) q.pop();
            if (q.empty()) asks.erase(it);
        }
    }

    void cancel(uint64_t id, int side, int price) {
        // Naive cancel: scan the queue at that price level (O(n))
        if (side == 0) {
            auto it = bids.find(price);
            if (it == bids.end()) return;
            std::queue<NaiveOrder> filtered;
            while (!it->second.empty()) {
                auto o = it->second.front();
                it->second.pop();
                if (o.id != id) filtered.push(o);
            }
            it->second = std::move(filtered);
            if (it->second.empty()) bids.erase(it);
        } else {
            auto it = asks.find(price);
            if (it == asks.end()) return;
            std::queue<NaiveOrder> filtered;
            while (!it->second.empty()) {
                auto o = it->second.front();
                it->second.pop();
                if (o.id != id) filtered.push(o);
            }
            it->second = std::move(filtered);
            if (it->second.empty()) asks.erase(it);
        }
    }
};

static void BM_Naive_Insert(benchmark::State& state) {
    NaiveOrderBook book;
    for (auto _ : state) {
        state.PauseTiming();
        static uint64_t prev = 0;
        if (prev) book.cancel(prev, 0, 100);
        state.ResumeTiming();
        prev = book.insert(0, 100, 10);
    }
}
BENCHMARK(BM_Naive_Insert)->Unit(benchmark::kNanosecond);

static void BM_Naive_Match(benchmark::State& state) {
    NaiveOrderBook book;
    for (auto _ : state) {
        state.PauseTiming();
        book.insert(1, 100, 10);
        state.ResumeTiming();
        book.matchBuy(100, 10);
    }
}
BENCHMARK(BM_Naive_Match)->Unit(benchmark::kNanosecond);

static void BM_Naive_Cancel(benchmark::State& state) {
    NaiveOrderBook book;
    for (auto _ : state) {
        state.PauseTiming();
        uint64_t id = book.insert(0, 100, 10);
        state.ResumeTiming();
        book.cancel(id, 0, 100);
    }
}
BENCHMARK(BM_Naive_Cancel)->Unit(benchmark::kNanosecond);

// ---------------------------------------------
// BENCHMARK 1: INSERT LATENCY
// ---------------------------------------------
static void BM_InsertOrder(benchmark::State& state) {
    MatchingEngine engine;
    InstrumentId aapl = 1;

    // Pre-warm: insert and cancel enough orders to fill the pool's free-list
    // so every benchmark iteration reuses memory (steady state)
    for (int i = 0; i < 1000; ++i) {
        OrderId id = engine.addLimitOrder(aapl, Side::Buy, 100, 10, TimeInForce::GTC);
        engine.cancelOrder(aapl, id);
    }

    for (auto _ : state) {
        state.PauseTiming();
        // Cancel previous order so pool stays warm and book doesn't grow
        static OrderId prev = 0;
        if (prev) engine.cancelOrder(aapl, prev);
        state.ResumeTiming();

        prev = engine.addLimitOrder(aapl, Side::Buy, 100, 10, TimeInForce::GTC);
    }
}
BENCHMARK(BM_InsertOrder)->Unit(benchmark::kNanosecond);

// ---------------------------------------------
// BENCHMARK 2: MATCHING LATENCY
// ---------------------------------------------
static void BM_MatchOrder(benchmark::State& state) {
    MatchingEngine engine;
    InstrumentId aapl = 1;

    for (auto _ : state) {
        // 1. PAUSE the timer: We are setting up the state, don't measure this!
        state.PauseTiming();
        
        // Add one resting sell order
        engine.addLimitOrder(aapl, Side::Sell, 100, 10, TimeInForce::GTC);
        
        // 2. RESUME the timer: Start measuring
        state.ResumeTiming();

        // Fire one incoming market buy order to instantly match the sell we just added
        engine.addMarketOrder(aapl, Side::Buy, 10);
    }
}
BENCHMARK(BM_MatchOrder)->Unit(benchmark::kNanosecond);

// ---------------------------------------------
// BENCHMARK 3: CANCEL LATENCY
// ---------------------------------------------
static void BM_CancelOrder(benchmark::State& state) {
    MatchingEngine engine;
    InstrumentId aapl = 1;

    for (auto _ : state) {
        // Pause: insert a resting order to cancel (don't measure this)
        state.PauseTiming();
        OrderId id = engine.addLimitOrder(aapl, Side::Buy, 100, 10, TimeInForce::GTC);
        state.ResumeTiming();

        // Measure: just the cancel
        engine.cancelOrder(aapl, id);
    }
}
BENCHMARK(BM_CancelOrder)->Unit(benchmark::kNanosecond);

// ---------------------------------------------
// BENCHMARK: CANCEL UNDER CONTENTION
// Shows O(1) vs O(n) gap clearly
// ---------------------------------------------
static void BM_CancelOrder_Contention(benchmark::State& state) {
    MatchingEngine engine;
    InstrumentId aapl = 1;
    const int N = 1000;

    // Fill one price level with N resting orders
    std::vector<OrderId> ids;
    ids.reserve(N);
    for (int i = 0; i < N; ++i)
        ids.push_back(engine.addLimitOrder(aapl, Side::Buy, 100, 10, TimeInForce::GTC));

    int idx = 0;
    for (auto _ : state) {
        state.PauseTiming();
        // Re-insert if we've exhausted the list
        if (idx >= (int)ids.size()) {
            ids.clear();
            for (int i = 0; i < N; ++i)
                ids.push_back(engine.addLimitOrder(aapl, Side::Buy, 100, 10, TimeInForce::GTC));
            idx = 0;
        }
        state.ResumeTiming();

        // Cancel the last-inserted order (worst case for naive: scan entire queue)
        engine.cancelOrder(aapl, ids[idx++]);
    }
}
BENCHMARK(BM_CancelOrder_Contention)->Unit(benchmark::kNanosecond);

static void BM_Naive_Cancel_Contention(benchmark::State& state) {
    NaiveOrderBook book;
    const int N = 1000;

    std::vector<uint64_t> ids;
    ids.reserve(N);
    for (int i = 0; i < N; ++i)
        ids.push_back(book.insert(0, 100, 10));

    int idx = 0;
    for (auto _ : state) {
        state.PauseTiming();
        if (idx >= (int)ids.size()) {
            ids.clear();
            for (int i = 0; i < N; ++i)
                ids.push_back(book.insert(0, 100, 10));
            idx = 0;
        }
        state.ResumeTiming();

        book.cancel(ids[idx++], 0, 100);
    }
}
BENCHMARK(BM_Naive_Cancel_Contention)->Unit(benchmark::kNanosecond);

// Execute the benchmark runner
BENCHMARK_MAIN();