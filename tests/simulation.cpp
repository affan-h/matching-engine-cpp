#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include "matching_engine.h"
#include "spsc_queue.h"

using namespace std;
using namespace std::chrono;

// This atomic flag allows the Producer to safely tell the Consumer to shut down
std::atomic<bool> is_running{true};

// ---------------------------------------------------------
// THE CONSUMER (MATCHING ENGINE CORE)
// ---------------------------------------------------------
void runMatchingEngine(SPSCQueue& queue, MatchingEngine& engine) {
    OrderEvent event;

    // The "Spin Loop" - In low-latency systems, this thread is pinned to an 
    // isolated CPU core and NEVER sleeps or yields. It aggressively polls the queue.
    while (true) {
        if (queue.pop(event)) {
            // We got an order! Route it to the correct engine function.
            switch (event.type) {
                case EventType::LimitOrder:
                    engine.addLimitOrder(event.instrument, event.side, event.price, event.qty, event.tif);
                    break;
                case EventType::MarketOrder:
                    engine.addMarketOrder(event.instrument, event.side, event.qty);
                    break;
                case EventType::CancelOrder:
                    engine.cancelOrder(event.instrument, event.id);
                    break;
                case EventType::ModifyOrder:
                    engine.modifyOrder(event.instrument, event.id, event.price, event.qty);
                    break;
            }
        } else {
            // The queue is empty.
            // Check if the gateway has signaled a shutdown.
            if (!is_running.load(std::memory_order_relaxed)) {
                break; // Exit the endless loop
            }
        }
    }
}

// ---------------------------------------------------------
// THE PRODUCER (NETWORK GATEWAY SIMULATOR)
// ---------------------------------------------------------
void simulateNetworkTraffic(
    SPSCQueue& queue,
    int num_orders,
    InstrumentId AAPL,
    InstrumentId RELIANCE,
    InstrumentId INFY,
    InstrumentId TATASTEEL)
{
    // Each instrument trades around a different reference price
    // to feel realistic
    struct InstrumentProfile {
        InstrumentId id;
        int basePrice;
    };

    InstrumentProfile profiles[] = {
        {AAPL,      150},
        {RELIANCE,  2800},
        {INFY,      1400},
        {TATASTEEL, 140}
    };

    auto start = high_resolution_clock::now();

    for (int i = 0; i < num_orders; ++i) {
        auto& prof = profiles[i % 4];   // round-robin across instruments
        OrderEvent event;
        event.instrument = prof.id;
        event.qty = 10;

        if (i % 3 == 0) {
            event.type = EventType::LimitOrder;
            event.side = Side::Buy;
            event.price = prof.basePrice;
            event.tif = TimeInForce::GTC;
        } else if (i % 3 == 1) {
            event.type = EventType::MarketOrder;
            event.side = Side::Sell;
            event.price = 0;
            event.tif = TimeInForce::IOC;
        } else {
            event.type = EventType::LimitOrder;
            event.side = Side::Sell;
            event.price = prof.basePrice - 1;  // aggressive sell, crosses spread
            event.tif = TimeInForce::IOC;
        }

        while (!queue.push(event)) {}
    }

    auto end = high_resolution_clock::now();
    auto ms = duration_cast<milliseconds>(end - start).count();
    cout << "[Gateway] Pushed " << num_orders
         << " orders across 4 instruments in " << ms << " ms.\n";

    is_running.store(false, std::memory_order_relaxed);
}

// ---------------------------------------------------------
// MAIN ENTRY POINT
// ---------------------------------------------------------
// In main(), register instruments before spawning threads:
int main() {
    cout << "===== Multi-Threaded Matching Engine Simulation =====\n\n";

    SPSCQueue queue(1'000'000);
    MatchingEngine engine;

    // Register instruments upfront
    InstrumentId AAPL     = engine.registerInstrument("AAPL");
    InstrumentId RELIANCE = engine.registerInstrument("RELIANCE");
    InstrumentId INFY     = engine.registerInstrument("INFY");
    InstrumentId TATASTEEL= engine.registerInstrument("TATASTEEL");

    int total_orders = 5'000'000;

    std::atomic<uint64_t> snapshotCount{0};

    engine.subscribeMarketData([&](const L2Snapshot& snap) {
        uint64_t n = ++snapshotCount;
        if (n % 500000 == 0) {  // print every 500,000th snapshot
            std::cout << "[Feed] " << snap.symbol
                    << "  bid=" << snap.bestBid()
                    << "  ask=" << snap.bestAsk()
                    << "  spread=" << snap.spread()
                    << "  mid="   << snap.midPrice()
                    << "\n";
        }
    });

    auto start = high_resolution_clock::now();
    std::thread engine_thread(runMatchingEngine, std::ref(queue), std::ref(engine));
    simulateNetworkTraffic(queue, total_orders, AAPL, RELIANCE, INFY, TATASTEEL);
    engine_thread.join();
    auto end = high_resolution_clock::now();
    auto total_ms = duration_cast<milliseconds>(end - start).count();

    cout << "[System] Engine shut down gracefully.\n\n";
    cout << "===== Final Report =====\n";
    cout << "Orders submitted : " << total_orders << "\n";
    cout << "Trades executed  : " << engine.getTotalTrades() << "\n";
    cout << "Total time       : " << total_ms << " ms\n\n";

    // Print book for each instrument
    for (const string& sym : {"AAPL", "RELIANCE", "INFY", "TATASTEEL"}) {
        cout << "===== Order Book: " << sym << " =====\n";
        engine.printOrderBook(engine.getInstrumentId(sym));
    }

    return 0;
}