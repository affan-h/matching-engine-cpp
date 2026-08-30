#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <thread>
#include <atomic>
#include "types.h"
#include "network_protocol.h"
#include "spsc_queue.h"
#include "matching_engine.h"
#include "tcp_parser.h"
#include "read_model.h"

// ─────────────────────────────────────────────
// Gateway Configuration & Statistics
// ─────────────────────────────────────────────

struct GatewayConfig {
    std::string host = "127.0.0.1";
    int port = 12345;                    // 0 = ephemeral port (useful for tests)
    size_t max_client_buffer = 16384;    // 16 KB max per-client parser buffer
    int max_backpressure_retries = 1000; // Bounded retries when SPSC queue is full
};

struct GatewayStats {
    std::atomic<uint64_t> connections_accepted{0};
    std::atomic<uint64_t> connections_closed{0};
    std::atomic<uint64_t> events_pushed{0};
    std::atomic<uint64_t> events_processed{0};
    std::atomic<uint64_t> queries_processed{0};
    std::atomic<uint64_t> session_frames_processed{0};
    std::atomic<uint64_t> malformed_frames{0};
    std::atomic<uint64_t> buffer_overflows{0};
    std::atomic<uint64_t> queue_full_drops{0};
};

// ─────────────────────────────────────────────
// Per-Client Connection State
// ─────────────────────────────────────────────

struct ClientConnection {
    int fd = -1;
    TcpParser parser;
};

// ─────────────────────────────────────────────
// TCP Gateway (macOS kqueue non-blocking I/O)
// ─────────────────────────────────────────────

class TcpGateway {
private:
    MatchingEngine& engine;
    SPSCQueue&      queue;
    GatewayConfig   config;
    ReadModel*      read_model{nullptr};

    int listen_fd = -1;
    int kq_fd     = -1;
    int bound_port = 0;

    std::atomic<bool> is_running{false};
    std::thread       gateway_thread;
    std::thread       consumer_thread;

    std::unordered_map<int, ClientConnection> clients;
    GatewayStats stats;

    void runGateway();
    void runConsumer();
    void closeClient(int fd);
    void handleSession(int fd, const SessionFrame& session);
    void handleQuery(int fd, const QueryFrame& query);

public:
    TcpGateway(
        MatchingEngine& engine,
        SPSCQueue& queue,
        const GatewayConfig& config = {},
        ReadModel* read_model = nullptr
    );
    ~TcpGateway();

    // Starts gateway event loop and matching engine consumer thread
    bool start();

    // Stops gateway and consumer cleanly, closing all client connections
    void stop();

    // Read Model Configuration
    void setReadModel(ReadModel* model) { read_model = model; }
    ReadModel* getReadModel() const { return read_model; }

    // Accessors
    bool isRunning() const { return is_running.load(); }
    int getBoundPort() const { return bound_port; }
    size_t getClientCount() const { return clients.size(); }
    const GatewayStats& getStats() const { return stats; }
};
