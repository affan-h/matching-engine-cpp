#include <iostream>
#include <csignal>
#include <chrono>
#include <thread>
#include "matching_engine.h"
#include "spsc_queue.h"
#include "stats_tracker.h"
#include "tcp_gateway.h"

static std::atomic<bool> g_shutdown{false};

static void handle_signal(int) {
    g_shutdown.store(true);
}

int main(int argc, char* argv[]) {
    int port = 12345;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "========================================\n";
    std::cout << "     Matching Engine TCP Gateway        \n";
    std::cout << "========================================\n\n";

    MatchingEngine engine;

    // Register standard initial instruments
    InstrumentId aapl      = engine.registerInstrument("AAPL");
    InstrumentId reliance  = engine.registerInstrument("RELIANCE");
    InstrumentId infy      = engine.registerInstrument("INFY");
    InstrumentId tatasteel = engine.registerInstrument("TATASTEEL");

    std::cout << "[Instruments Registered]\n";
    std::cout << "  ID " << aapl << ": AAPL\n";
    std::cout << "  ID " << reliance << ": RELIANCE\n";
    std::cout << "  ID " << infy << ": INFY\n";
    std::cout << "  ID " << tatasteel << ": TATASTEEL\n\n";

    StatsTracker tracker;
    engine.subscribeMarketData([&](const L2Snapshot& snap) {
        tracker.onSnapshot(snap);
    });
    engine.subscribeTradeData([&](InstrumentId id, const std::string& sym,
                                  Price px, Quantity qty, [[maybe_unused]] Side side) {
        tracker.recordTrade(id, sym, px, qty);
        std::cout << "[Trade] Symbol: " << sym
                  << " | Px: " << px
                  << " | Qty: " << qty
                  << " | Side: " << ((side == Side::Buy) ? "BUY" : "SELL")
                  << "\n";
    });

    // 64K slot lock-free SPSC queue
    SPSCQueue queue(65536);

    GatewayConfig config;
    config.host = "127.0.0.1";
    config.port = port;
    config.max_client_buffer = 16384;
    config.max_backpressure_retries = 1000;

    TcpGateway gateway(engine, queue, config);

    if (!gateway.start()) {
        std::cerr << "[Fatal] Failed to start TCP Gateway on " << config.host << ":" << port << "\n";
        return 1;
    }

    std::cout << "[Gateway Started] Listening on " << config.host << ":" << gateway.getBoundPort() << "\n";
    std::cout << "Ready for incoming client connections. Press Ctrl+C to stop.\n\n";

    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\n[Gateway] Shutting down gracefully...\n";
    gateway.stop();

    const auto& stats = gateway.getStats();
    std::cout << "\n===== Gateway Statistics =====\n";
    std::cout << "Connections accepted : " << stats.connections_accepted.load() << "\n";
    std::cout << "Connections closed   : " << stats.connections_closed.load() << "\n";
    std::cout << "Events pushed        : " << stats.events_pushed.load() << "\n";
    std::cout << "Events processed     : " << stats.events_processed.load() << "\n";
    std::cout << "Malformed frames     : " << stats.malformed_frames.load() << "\n";
    std::cout << "Buffer overflows     : " << stats.buffer_overflows.load() << "\n";
    std::cout << "Queue drops          : " << stats.queue_full_drops.load() << "\n";
    std::cout << "Total trades         : " << engine.getTotalTrades() << "\n\n";

    tracker.printAll();

    return 0;
}
