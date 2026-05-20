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
void simulateNetworkTraffic(SPSCQueue& queue, int num_orders) {
    InstrumentId aapl = 1;
    auto start = high_resolution_clock::now();

    for (int i = 0; i < num_orders; ++i) {
        OrderEvent event;
        event.instrument = aapl;
        event.qty = 10;
        
        if (i % 3 == 0) {
            event.type = EventType::LimitOrder;
            event.side = Side::Buy;
            event.price = 100;
            event.tif = TimeInForce::GTC;
        } else if (i % 3 == 1) {
            event.type = EventType::MarketOrder;
            event.side = Side::Sell;
            event.price = 0;
            event.tif = TimeInForce::IOC; // Market orders are effectively IOCs anyway
        } else {
            event.type = EventType::LimitOrder;
            event.side = Side::Sell;
            event.price = 99; // Aggressive sell
            event.tif = TimeInForce::IOC; // Match what you can, drop the rest
        }

        // Push to the ring buffer. 
        // If the queue is full (Matcher is falling behind), we busy-wait until space frees up.
        while (!queue.push(event)) {
            // In a real system, you would log a critical "backpressure" warning here.
        }
    }

    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start).count();
    
    cout << "[Gateway] Successfully pushed " << num_orders << " orders to the Ring Buffer in " << duration << " ms.\n";
    
    // Signal the matcher thread that no more orders are coming.
    is_running.store(false, std::memory_order_relaxed);
}

// ---------------------------------------------------------
// MAIN ENTRY POINT
// ---------------------------------------------------------
int main() {
    cout << "===== Multi-Threaded Matching Engine Simulation =====\n\n";

    SPSCQueue queue(1'000'000);
    MatchingEngine engine;
    int total_orders = 5'000'000;

    auto start = high_resolution_clock::now();

    std::thread engine_thread(runMatchingEngine, std::ref(queue), std::ref(engine));
    simulateNetworkTraffic(queue, total_orders);
    engine_thread.join();

    auto end = high_resolution_clock::now();
    auto total_ms = duration_cast<milliseconds>(end - start).count();

    cout << "[System] Engine drained the queue and shut down gracefully.\n\n";
    cout << "===== Final Report =====\n";
    cout << "Orders submitted : " << total_orders << "\n";
    cout << "Trades executed  : " << engine.getTotalTrades() << "\n";
    cout << "Total time       : " << total_ms << " ms\n";
    cout << "Throughput       : " << (total_orders * 1000 / total_ms) << " orders/sec\n\n";

    cout << "===== Final Order Book (Instrument 1) =====\n";
    engine.printOrderBook(1);

    return 0;
}