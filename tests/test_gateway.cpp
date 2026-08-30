#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>
#include "matching_engine.h"
#include "spsc_queue.h"
#include "wire_protocol.h"
#include "tcp_gateway.h"

// ─────────────────────────────────────────────
// Test Harness Helpers
// ─────────────────────────────────────────────

static int passed = 0;
static int failed = 0;

#define TEST(name) void name()
#define RUN(name)  do { \
    try { name(); \
        std::cout << "  PASS  " << #name << "\n"; ++passed; } \
    catch (const std::exception& e) { \
        std::cout << "  FAIL  " << #name << " — " << e.what() << "\n"; ++failed; } \
} while(0)

#define ASSERT(cond, msg) \
    if (!(cond)) throw std::runtime_error(msg)

static int connect_client(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket() failed");

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        throw std::runtime_error("connect() failed");
    }
    return fd;
}

static void send_all(int fd, const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t n = send(fd, p, remaining, 0);
        if (n <= 0) throw std::runtime_error("send() failed");
        p += n;
        remaining -= n;
    }
}

static void send_all(int fd, const std::vector<uint8_t>& bytes) {
    send_all(fd, bytes.data(), bytes.size());
}

// ─────────────────────────────────────────────
// 1. Server Starts and Stops
// ─────────────────────────────────────────────
TEST(test_server_starts_and_stops) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0; // Ephemeral port

    TcpGateway gateway(engine, queue, config);
    ASSERT(gateway.start(), "Gateway failed to start");
    ASSERT(gateway.isRunning(), "Gateway should report running");
    ASSERT(gateway.getBoundPort() > 0, "Gateway should have non-zero bound port");

    gateway.stop();
    ASSERT(!gateway.isRunning(), "Gateway should report stopped");
}

// ─────────────────────────────────────────────
// 2. Client Connects and Disconnects
// ─────────────────────────────────────────────
TEST(test_client_connects_and_disconnects) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    ASSERT(gateway.start(), "Gateway failed to start");

    int client_fd = connect_client(gateway.getBoundPort());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    ASSERT(gateway.getStats().connections_accepted.load() == 1, "Expected 1 connection accepted");

    close(client_fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(gateway.getStats().connections_closed.load() == 1, "Expected 1 connection closed");
    gateway.stop();
}

// ─────────────────────────────────────────────
// 3. Limit Order Reaches Engine
// ─────────────────────────────────────────────
TEST(test_limit_order_reaches_engine) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    // Send Limit Buy AAPL 100 @ 10
    auto buy_frame = wire::encode_limit_order(aapl, Side::Buy, 100, 10, TimeInForce::GTC);
    send_all(client, buy_frame);

    // Send Limit Sell AAPL 100 @ 10 (Matches!)
    auto sell_frame = wire::encode_limit_order(aapl, Side::Sell, 100, 10, TimeInForce::GTC);
    send_all(client, sell_frame);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(engine.getTotalTrades() == 1, "Expected 1 trade executed in MatchingEngine");
    ASSERT(gateway.getStats().events_processed.load() == 2, "Expected 2 events processed");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 4. Market Order Reaches Engine
// ─────────────────────────────────────────────
TEST(test_market_order_reaches_engine) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    // Resting Sell @ 150
    auto sell_frame = wire::encode_limit_order(aapl, Side::Sell, 150, 20, TimeInForce::GTC);
    send_all(client, sell_frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Incoming Market Buy
    auto market_frame = wire::encode_market_order(aapl, Side::Buy, 20);
    send_all(client, market_frame);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(engine.getTotalTrades() == 1, "Expected Market Order match in engine");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 5. Cancel Order Reaches Engine
// ─────────────────────────────────────────────
TEST(test_cancel_order_reaches_engine) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    // Limit Buy (Order ID will be 1)
    auto buy_frame = wire::encode_limit_order(aapl, Side::Buy, 100, 10, TimeInForce::GTC);
    send_all(client, buy_frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Cancel Order ID 1
    auto cancel_frame = wire::encode_cancel_order(aapl, 1ULL);
    send_all(client, cancel_frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Send Limit Sell at same price (Should NOT trade since buy was cancelled)
    auto sell_frame = wire::encode_limit_order(aapl, Side::Sell, 100, 10, TimeInForce::GTC);
    send_all(client, sell_frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(engine.getTotalTrades() == 0, "Cancelled order must not execute trades");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 6. Modify Order Reaches Engine
// ─────────────────────────────────────────────
TEST(test_modify_order_reaches_engine) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    // Limit Buy 100 @ 10 (Order ID 1)
    auto buy_frame = wire::encode_limit_order(aapl, Side::Buy, 100, 10, TimeInForce::GTC);
    send_all(client, buy_frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Modify Order ID 1 down to qty 5
    auto mod_frame = wire::encode_modify_order(aapl, 1ULL, 100, 5);
    send_all(client, mod_frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Send Sell @ 100 for qty 5 (Fully fills modified buy order)
    auto sell_frame1 = wire::encode_limit_order(aapl, Side::Sell, 100, 5, TimeInForce::GTC);
    send_all(client, sell_frame1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT(engine.getTotalTrades() == 1, "First trade must execute");

    // Send another Sell @ 100 for qty 5 (Should NOT match because original 10 qty was reduced to 5)
    auto sell_frame2 = wire::encode_limit_order(aapl, Side::Sell, 100, 5, TimeInForce::GTC);
    send_all(client, sell_frame2);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ASSERT(engine.getTotalTrades() == 1, "Book should have 0 remaining bid volume after modify");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 7. Fragmented Frame
// ─────────────────────────────────────────────
TEST(test_fragmented_frame) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    auto frame = wire::encode_limit_order(aapl, Side::Buy, 200, 15, TimeInForce::GTC);

    // Send in 3 arbitrary fragments
    send_all(client, frame.data(), 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    send_all(client, frame.data() + 2, 7);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    send_all(client, frame.data() + 9, frame.size() - 9);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Match it
    auto match_frame = wire::encode_limit_order(aapl, Side::Sell, 200, 15, TimeInForce::GTC);
    send_all(client, match_frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(engine.getTotalTrades() == 1, "Fragmented frame must be reconstructed and matched");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 8. One-Byte-At-A-Time Frame
// ─────────────────────────────────────────────
TEST(test_one_byte_at_a_time_frame) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    auto buy_frame = wire::encode_limit_order(aapl, Side::Buy, 300, 5, TimeInForce::GTC);
    for (uint8_t byte : buy_frame) {
        send_all(client, &byte, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto sell_frame = wire::encode_limit_order(aapl, Side::Sell, 300, 5, TimeInForce::GTC);
    for (uint8_t byte : sell_frame) {
        send_all(client, &byte, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ASSERT(engine.getTotalTrades() == 1, "1-byte-at-a-time frames must execute correctly");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 9. Multiple Frames in One Send
// ─────────────────────────────────────────────
TEST(test_multiple_frames_in_one_send) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    auto f1 = wire::encode_limit_order(aapl, Side::Buy, 100, 10, TimeInForce::GTC);
    auto f2 = wire::encode_limit_order(aapl, Side::Sell, 100, 10, TimeInForce::GTC);

    std::vector<uint8_t> batch;
    batch.insert(batch.end(), f1.begin(), f1.end());
    batch.insert(batch.end(), f2.begin(), f2.end());

    send_all(client, batch);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(engine.getTotalTrades() == 1, "Coalesced frames in single recv must match");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 10. Multiple Frames With Partial Final Frame
// ─────────────────────────────────────────────
TEST(test_multiple_frames_with_partial_final_frame) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    auto f1 = wire::encode_limit_order(aapl, Side::Buy, 100, 10, TimeInForce::GTC);
    auto f2 = wire::encode_limit_order(aapl, Side::Sell, 100, 10, TimeInForce::GTC);
    auto f3 = wire::encode_limit_order(aapl, Side::Buy, 200, 5, TimeInForce::GTC);

    std::vector<uint8_t> chunk1;
    chunk1.insert(chunk1.end(), f1.begin(), f1.end());
    chunk1.insert(chunk1.end(), f2.begin(), f2.end());
    // Append partial header of f3 (only 2 bytes)
    chunk1.insert(chunk1.end(), f3.begin(), f3.begin() + 2);

    send_all(client, chunk1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    ASSERT(engine.getTotalTrades() == 1, "First 2 frames should have traded");

    // Send remaining bytes of f3
    send_all(client, f3.data() + 2, f3.size() - 2);

    // Send matching sell for f3
    auto f4 = wire::encode_limit_order(aapl, Side::Sell, 200, 5, TimeInForce::GTC);
    send_all(client, f4);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(engine.getTotalTrades() == 2, "3rd and 4th frames should have executed second trade");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 11. Malformed Frame Length
// ─────────────────────────────────────────────
TEST(test_malformed_frame) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    // Bad frame: Limit order with declared payload_len = 13 (expected 14)
    std::vector<uint8_t> bad_frame(wire::HEADER_SIZE + 13, 0);
    wire::write_u16_be(&bad_frame[0], 13);
    bad_frame[2] = static_cast<uint8_t>(wire::MessageType::NewLimitOrder);

    send_all(client, bad_frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(gateway.getStats().malformed_frames.load() >= 1, "Expected malformed frame detected");
    ASSERT(gateway.getStats().connections_closed.load() >= 1, "Connection should be closed on malformed frame");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 12. Oversized Frame
// ─────────────────────────────────────────────
TEST(test_oversized_frame) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    // Declared payload length 100 > MAX_PAYLOAD_LENGTH (64)
    std::vector<uint8_t> frame(wire::HEADER_SIZE + 100, 0);
    wire::write_u16_be(&frame[0], 100);
    frame[2] = static_cast<uint8_t>(wire::MessageType::NewLimitOrder);

    send_all(client, frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(gateway.getStats().malformed_frames.load() >= 1, "Expected malformed frame counter incremented");
    ASSERT(gateway.getStats().connections_closed.load() >= 1, "Server should disconnect client with oversized frame");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 13. Unknown Message Type
// ─────────────────────────────────────────────
TEST(test_unknown_message_type) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    std::vector<uint8_t> frame(wire::HEADER_SIZE + 10, 0);
    wire::write_u16_be(&frame[0], 10);
    frame[2] = 0xAA; // Unknown type

    send_all(client, frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(gateway.getStats().malformed_frames.load() >= 1, "Unknown message type rejected");
    ASSERT(gateway.getStats().connections_closed.load() >= 1, "Connection closed");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 14. Client Disconnect
// ─────────────────────────────────────────────
TEST(test_client_disconnect_handling) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT(gateway.getStats().connections_accepted.load() == 1, "Connected");

    close(client);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(gateway.getStats().connections_closed.load() == 1, "Closed recorded");
    ASSERT(gateway.getClientCount() == 0, "No remaining clients in map");

    gateway.stop();
}

// ─────────────────────────────────────────────
// 15. Disconnect During Partial Frame
// ─────────────────────────────────────────────
TEST(test_disconnect_during_partial_frame) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    auto frame = wire::encode_limit_order(aapl, Side::Buy, 100, 10, TimeInForce::GTC);
    // Send only 6 bytes, then immediately close
    send_all(client, frame.data(), 6);
    close(client);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(engine.getTotalTrades() == 0, "No partial trades executed");
    ASSERT(gateway.getStats().connections_closed.load() == 1, "Connection cleanly closed");

    gateway.stop();
}

// ─────────────────────────────────────────────
// 16. Multiple Simultaneous Clients
// ─────────────────────────────────────────────
TEST(test_multiple_simultaneous_clients) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    InstrumentId reliance = engine.registerInstrument("RELIANCE");
    SPSCQueue queue(65536);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    const int num_clients = 4;
    std::vector<int> client_fds;
    for (int i = 0; i < num_clients; ++i) {
        client_fds.push_back(connect_client(gateway.getBoundPort()));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT(gateway.getStats().connections_accepted.load() == num_clients, "All 4 clients connected");

    // Client 0 sends 50 Buys on AAPL
    // Client 1 sends 50 Sells on AAPL (Should match 50 trades)
    // Client 2 sends 50 Buys on RELIANCE
    // Client 3 sends 50 Sells on RELIANCE (Should match 50 trades)
    std::vector<std::thread> workers;
    for (int i = 0; i < num_clients; ++i) {
        workers.emplace_back([i, &client_fds, aapl, reliance]() {
            int fd = client_fds[i];
            InstrumentId inst = (i < 2) ? aapl : reliance;
            Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
            Price px = (inst == aapl) ? 150 : 2800;

            for (int j = 0; j < 50; ++j) {
                auto frame = wire::encode_limit_order(inst, side, px, 10, TimeInForce::GTC);
                send_all(fd, frame);
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT(engine.getTotalTrades() == 100, "Expected 100 total trades across all clients");

    for (int fd : client_fds) {
        close(fd);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    gateway.stop();
}

// ─────────────────────────────────────────────
// 17. Queue Full Behavior
// ─────────────────────────────────────────────
TEST(test_queue_full_behavior) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    // Tiny SPSC queue with capacity 4
    SPSCQueue queue(4);
    GatewayConfig config;
    config.port = 0;
    config.max_backpressure_retries = 5; // low retries to easily trigger overflow under saturation

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    // Blast orders rapidly into the tiny queue
    for (int i = 0; i < 50; ++i) {
        auto frame = wire::encode_limit_order(aapl, Side::Buy, 100, 1, TimeInForce::GTC);
        try {
            send_all(client, frame);
        } catch (...) {
            break; // connection might be closed by backpressure policy
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Gateway must not crash, and either processed events or handled backpressure drops
    ASSERT(gateway.getStats().events_processed.load() > 0 ||
           gateway.getStats().queue_full_drops.load() > 0, "Queue handled events / backpressure");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 18. Connection Cleanup & Buffer Overflow
// ─────────────────────────────────────────────
TEST(test_buffer_overflow_protection) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;
    config.max_client_buffer = 100; // Artificially small 100 bytes buffer limit

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    // Send 200 bytes of incomplete garbage to trigger client buffer overflow
    std::vector<uint8_t> garbage(200, 0);
    send_all(client, garbage);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(gateway.getStats().buffer_overflows.load() >= 1, "Buffer overflow should be recorded");
    ASSERT(gateway.getStats().connections_closed.load() >= 1, "Client should be disconnected on buffer overflow");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// Main Test Runner
// ─────────────────────────────────────────────
int main() {
    std::signal(SIGPIPE, SIG_IGN);
    std::cout << "\n===== TCP Gateway & kqueue Integration Test Suite =====\n\n";

    RUN(test_server_starts_and_stops);
    RUN(test_client_connects_and_disconnects);
    RUN(test_limit_order_reaches_engine);
    RUN(test_market_order_reaches_engine);
    RUN(test_cancel_order_reaches_engine);
    RUN(test_modify_order_reaches_engine);
    RUN(test_fragmented_frame);
    RUN(test_one_byte_at_a_time_frame);
    RUN(test_multiple_frames_in_one_send);
    RUN(test_multiple_frames_with_partial_final_frame);
    RUN(test_malformed_frame);
    RUN(test_oversized_frame);
    RUN(test_unknown_message_type);
    RUN(test_client_disconnect_handling);
    RUN(test_disconnect_during_partial_frame);
    RUN(test_multiple_simultaneous_clients);
    RUN(test_queue_full_behavior);
    RUN(test_buffer_overflow_protection);

    std::cout << "\n=======================================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n\n";

    return (failed == 0) ? 0 : 1;
}
