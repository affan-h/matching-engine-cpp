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
    size_t max_connections = 1024;       // Max concurrent active client connections
    bool enable_logging = false;         // Enable structured diagnostic error logging

    bool isValid(std::string* error_msg = nullptr) const {
        if (port < 0 || port > 65535) {
            if (error_msg) *error_msg = "Port must be in range 0..65535";
            return false;
        }
        if (max_client_buffer < 64 || max_client_buffer > 1048576) {
            if (error_msg) *error_msg = "max_client_buffer must be between 64 and 1048576 bytes";
            return false;
        }
        if (max_backpressure_retries < 1 || max_backpressure_retries > 100000) {
            if (error_msg) *error_msg = "max_backpressure_retries must be between 1 and 100000";
            return false;
        }
        if (max_connections == 0 || max_connections > 65536) {
            if (error_msg) *error_msg = "max_connections must be between 1 and 65536";
            return false;
        }
        return true;
    }
};

struct GatewayStats {
    std::atomic<uint64_t> connections_accepted{0};
    std::atomic<uint64_t> connections_closed{0};
    std::atomic<uint64_t> connections_rejected{0};
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

    std::atomic<int> listen_fd{-1};
    std::atomic<int> kq_fd{-1};
    int bound_port = 0;

    std::atomic<bool>   is_running{false};
    std::atomic<bool>   worker_fault{false};
    std::atomic<size_t> active_clients{0};
    std::thread         gateway_thread;
    std::thread         consumer_thread;

    std::unordered_map<int, ClientConnection> clients;
    GatewayStats stats;

    void runGateway();
    void runConsumer();
    void closeClient(int fd);
    bool handleSession(int fd, const SessionFrame& session);
    bool handleQuery(int fd, const QueryFrame& query);

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
    bool isHealthy() const { return is_running.load() && !worker_fault.load(); }
    int getBoundPort() const { return bound_port; }
    size_t getClientCount() const { return active_clients.load(std::memory_order_relaxed); }
    const GatewayStats& getStats() const { return stats; }
};
