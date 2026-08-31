#include <iostream>
#include <cstdlib>
#include <string>
#include <csignal>
#include <chrono>
#include <thread>
#include "matching_engine.h"
#include "spsc_queue.h"
#include "stats_tracker.h"
#include "tcp_gateway.h"
#include "read_model.h"
#include "projector.h"

static std::atomic<bool> g_shutdown{false};

static void handle_signal(int) {
    g_shutdown.store(true);
}

static inline bool is_power_of_two(size_t n) {
    return n >= 2 && (n & (n - 1)) == 0;
}

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -p, --port <port>             TCP port to bind (default: 12345 or $MATCHING_ENGINE_GATEWAY_PORT)\n"
              << "  -h, --host <host>             Host address to bind (default: 127.0.0.1 or $MATCHING_ENGINE_GATEWAY_HOST)\n"
              << "      --command-queue <size>    SPSC command queue capacity (power of 2, 1024..1048576, default: 65536)\n"
              << "      --outbound-queue <size>   Outbound event queue capacity (power of 2, 1024..1048576, default: 65536)\n"
              << "      --trade-history <cap>     Recent trade history capacity per symbol (10..1000000, default: 1000)\n"
              << "      --order-history <cap>     Order record state history capacity (10..1000000, default: 10000)\n"
              << "      --max-connections <n>     Max concurrent client connections (1..65536, default: 1024)\n"
              << "  -v, --verbose                 Enable detailed operational error logging\n"
              << "      --help                    Display this help message and exit\n";
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 12345;
    size_t command_queue_cap = 65536;
    size_t outbound_queue_cap = 65536;
    size_t trade_history_cap = 1000;
    size_t order_history_cap = 10000;
    size_t max_connections = 1024;
    bool verbose = false;

    // 1. Read environment variable overrides
    if (const char* env_host = std::getenv("MATCHING_ENGINE_GATEWAY_HOST")) {
        host = env_host;
    }
    if (const char* env_port = std::getenv("MATCHING_ENGINE_GATEWAY_PORT")) {
        try {
            port = std::stoi(env_port);
        } catch (...) {
            std::cerr << "[Configuration Error] Invalid MATCHING_ENGINE_GATEWAY_PORT: " << env_port << "\n";
            return 1;
        }
    }
    if (const char* env_cq = std::getenv("MATCHING_ENGINE_COMMAND_QUEUE_SIZE")) {
        try {
            command_queue_cap = std::stoull(env_cq);
        } catch (...) {
            std::cerr << "[Configuration Error] Invalid MATCHING_ENGINE_COMMAND_QUEUE_SIZE: " << env_cq << "\n";
            return 1;
        }
    }
    if (const char* env_oq = std::getenv("MATCHING_ENGINE_OUTBOUND_QUEUE_SIZE")) {
        try {
            outbound_queue_cap = std::stoull(env_oq);
        } catch (...) {
            std::cerr << "[Configuration Error] Invalid MATCHING_ENGINE_OUTBOUND_QUEUE_SIZE: " << env_oq << "\n";
            return 1;
        }
    }
    if (const char* env_th = std::getenv("MATCHING_ENGINE_TRADE_HISTORY_CAP")) {
        try {
            trade_history_cap = std::stoull(env_th);
        } catch (...) {
            std::cerr << "[Configuration Error] Invalid MATCHING_ENGINE_TRADE_HISTORY_CAP: " << env_th << "\n";
            return 1;
        }
    }
    if (const char* env_oh = std::getenv("MATCHING_ENGINE_ORDER_HISTORY_CAP")) {
        try {
            order_history_cap = std::stoull(env_oh);
        } catch (...) {
            std::cerr << "[Configuration Error] Invalid MATCHING_ENGINE_ORDER_HISTORY_CAP: " << env_oh << "\n";
            return 1;
        }
    }

    // 2. Parse CLI arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            try {
                port = std::stoi(argv[++i]);
            } catch (...) {
                std::cerr << "[Configuration Error] Invalid port argument: " << argv[i] << "\n";
                return 1;
            }
        } else if ((arg == "-h" || arg == "--host") && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--command-queue" && i + 1 < argc) {
            try {
                command_queue_cap = std::stoull(argv[++i]);
            } catch (...) {
                std::cerr << "[Configuration Error] Invalid --command-queue argument: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--outbound-queue" && i + 1 < argc) {
            try {
                outbound_queue_cap = std::stoull(argv[++i]);
            } catch (...) {
                std::cerr << "[Configuration Error] Invalid --outbound-queue argument: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--trade-history" && i + 1 < argc) {
            try {
                trade_history_cap = std::stoull(argv[++i]);
            } catch (...) {
                std::cerr << "[Configuration Error] Invalid --trade-history argument: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--order-history" && i + 1 < argc) {
            try {
                order_history_cap = std::stoull(argv[++i]);
            } catch (...) {
                std::cerr << "[Configuration Error] Invalid --order-history argument: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--max-connections" && i + 1 < argc) {
            try {
                max_connections = std::stoull(argv[++i]);
            } catch (...) {
                std::cerr << "[Configuration Error] Invalid --max-connections argument: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg.rfind("-", 0) != 0 && i == 1 && argc == 2) {
            // Backwards compatibility: positional port argument
            try {
                port = std::stoi(arg);
            } catch (...) {
                std::cerr << "[Configuration Error] Invalid port: " << arg << "\n";
                return 1;
            }
        } else {
            std::cerr << "[Configuration Error] Unknown option or missing parameter: " << arg << "\n\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // 3. Deterministic configuration validation
    if (port < 0 || port > 65535) {
        std::cerr << "[Configuration Error] Port must be between 0 and 65535 (received: " << port << ")\n";
        return 1;
    }
    if (!is_power_of_two(command_queue_cap) || command_queue_cap < 1024 || command_queue_cap > 1048576) {
        std::cerr << "[Configuration Error] Command queue size must be a power of 2 between 1024 and 1048576 (received: "
                  << command_queue_cap << ")\n";
        return 1;
    }
    if (!is_power_of_two(outbound_queue_cap) || outbound_queue_cap < 1024 || outbound_queue_cap > 1048576) {
        std::cerr << "[Configuration Error] Outbound queue size must be a power of 2 between 1024 and 1048576 (received: "
                  << outbound_queue_cap << ")\n";
        return 1;
    }
    if (trade_history_cap < 10 || trade_history_cap > 1000000) {
        std::cerr << "[Configuration Error] Trade history capacity must be between 10 and 1000000 (received: "
                  << trade_history_cap << ")\n";
        return 1;
    }
    if (order_history_cap < 10 || order_history_cap > 1000000) {
        std::cerr << "[Configuration Error] Order history capacity must be between 10 and 1000000 (received: "
                  << order_history_cap << ")\n";
        return 1;
    }
    if (max_connections < 1 || max_connections > 65536) {
        std::cerr << "[Configuration Error] Max connections must be between 1 and 65536 (received: "
                  << max_connections << ")\n";
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "========================================\n";
    std::cout << "  Matching Engine Gateway & Read Model  \n";
    std::cout << "========================================\n\n";

    std::cout << "[Configuration]\n";
    std::cout << "  Host                  : " << host << "\n";
    std::cout << "  Port                  : " << port << (port == 0 ? " (ephemeral)" : "") << "\n";
    std::cout << "  Command Queue Cap     : " << command_queue_cap << "\n";
    std::cout << "  Outbound Queue Cap    : " << outbound_queue_cap << "\n";
    std::cout << "  Trade History Cap/Sym : " << trade_history_cap << "\n";
    std::cout << "  Order History Cap     : " << order_history_cap << "\n";
    std::cout << "  Max Client Conns      : " << max_connections << "\n";
    std::cout << "  Operational Logging   : " << (verbose ? "ENABLED" : "DISABLED") << "\n\n";

    MatchingEngine engine;
    ReadModel read_model(order_history_cap, trade_history_cap);

    // Register standard initial instruments in Engine and ReadModel
    InstrumentId aapl      = engine.registerInstrument("AAPL");
    InstrumentId reliance  = engine.registerInstrument("RELIANCE");
    InstrumentId infy      = engine.registerInstrument("INFY");
    InstrumentId tatasteel = engine.registerInstrument("TATASTEEL");

    read_model.registerSymbol(aapl, "AAPL");
    read_model.registerSymbol(reliance, "RELIANCE");
    read_model.registerSymbol(infy, "INFY");
    read_model.registerSymbol(tatasteel, "TATASTEEL");

    std::cout << "[Instruments Registered]\n";
    std::cout << "  ID " << aapl << ": AAPL\n";
    std::cout << "  ID " << reliance << ": RELIANCE\n";
    std::cout << "  ID " << infy << ": INFY\n";
    std::cout << "  ID " << tatasteel << ": TATASTEEL\n\n";

    // Setup Outbound SPSC Queue and Projector
    OutboundEventQueue outbound_queue(outbound_queue_cap);
    engine.setOutboundQueue(&outbound_queue);

    Projector projector(outbound_queue, read_model);
    if (!projector.start()) {
        std::cerr << "[Fatal] Failed to start Read Model Projector\n";
        return 1;
    }

    StatsTracker tracker;
    engine.subscribeMarketData([&](const L2Snapshot& snap) {
        tracker.onSnapshot(snap);
    });
    engine.subscribeTradeData([&](InstrumentId id, const std::string& sym,
                                  Price px, Quantity qty, [[maybe_unused]] Side side) {
        tracker.recordTrade(id, sym, px, qty);
        if (verbose) {
            std::cout << "[Trade] Symbol: " << sym
                      << " | Px: " << px
                      << " | Qty: " << qty
                      << " | Side: " << ((side == Side::Buy) ? "BUY" : "SELL")
                      << "\n";
        }
    });

    // Lock-free SPSC command queue
    SPSCQueue command_queue(command_queue_cap);

    GatewayConfig config;
    config.host = host;
    config.port = port;
    config.max_client_buffer = 16384;
    config.max_backpressure_retries = 1000;
    config.max_connections = max_connections;
    config.enable_logging = verbose;

    TcpGateway gateway(engine, command_queue, config, &read_model);

    if (!gateway.start()) {
        std::cerr << "[Fatal] Failed to start TCP Gateway on " << config.host << ":" << port << "\n";
        projector.stop();
        return 1;
    }

    std::cout << "[Gateway Started] Listening on " << config.host << ":" << gateway.getBoundPort() << "\n";
    std::cout << "Ready for incoming client connections (Commands & Queries). Press Ctrl+C to stop.\n\n";

    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\n[Gateway] Shutting down gracefully...\n";
    gateway.stop();
    projector.stop();

    const auto& stats = gateway.getStats();
    const auto& proj_stats = projector.getStats();

    std::cout << "\n===== Gateway & Read Model Statistics =====\n";
    std::cout << "Connections accepted : " << stats.connections_accepted.load() << "\n";
    std::cout << "Connections closed   : " << stats.connections_closed.load() << "\n";
    std::cout << "Connections rejected : " << stats.connections_rejected.load() << "\n";
    std::cout << "Commands pushed      : " << stats.events_pushed.load() << "\n";
    std::cout << "Commands processed   : " << stats.events_processed.load() << "\n";
    std::cout << "Queries processed    : " << stats.queries_processed.load() << "\n";
    std::cout << "Events projected     : " << proj_stats.events_projected.load() << "\n";
    std::cout << "Trades projected     : " << proj_stats.trades_projected.load() << "\n";
    std::cout << "L2 updates projected : " << proj_stats.l2_updates_projected.load() << "\n";
    std::cout << "Order states proj.   : " << proj_stats.order_states_projected.load() << "\n";
    std::cout << "Shutdown drain count : " << proj_stats.drained_on_shutdown.load() << "\n";
    std::cout << "Malformed frames     : " << stats.malformed_frames.load() << "\n";
    std::cout << "Buffer overflows     : " << stats.buffer_overflows.load() << "\n";
    std::cout << "Queue drops          : " << stats.queue_full_drops.load() << "\n";
    std::cout << "Total trades (Engine): " << engine.getTotalTrades() << "\n";
    std::cout << "Total volume (Engine): " << engine.getTotalVolume() << "\n";
    std::cout << "Orders accepted (Eng): " << engine.getTotalOrdersAccepted() << "\n";
    std::cout << "Orders rejected (Eng): " << engine.getTotalOrdersRejected() << "\n";
    std::cout << "Orders cancelled(Eng): " << engine.getTotalOrdersCancelled() << "\n";
    std::cout << "L2 coalesced drops   : " << engine.getL2CoalescedDrops() << "\n\n";

    tracker.printAll();

    return 0;
}
