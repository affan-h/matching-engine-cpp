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

static std::pair<wire::MessageType, std::vector<uint8_t>> recv_response(int fd) {
    uint8_t hdr[3];
    size_t got = 0;
    while (got < 3) {
        ssize_t n = recv(fd, hdr + got, 3 - got, 0);
        if (n <= 0) throw std::runtime_error("recv header failed");
        got += n;
    }
    uint16_t len = wire::read_u16_be(hdr);
    wire::MessageType type = static_cast<wire::MessageType>(hdr[2]);

    std::vector<uint8_t> payload(len);
    got = 0;
    while (got < len) {
        ssize_t n = recv(fd, payload.data() + got, len - got, 0);
        if (n <= 0) throw std::runtime_error("recv payload failed");
        got += n;
    }
    return {type, payload};
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

    ASSERT(gateway.getClientCount() == 1, "Expected 1 connected client");
    ASSERT(gateway.getStats().connections_accepted == 1, "Expected 1 accepted connection");

    close(client_fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    gateway.stop();
}

// ─────────────────────────────────────────────
// 3. Limit Order Reaches Engine
// ─────────────────────────────────────────────
TEST(test_limit_order_reaches_engine) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client_fd = connect_client(gateway.getBoundPort());

    auto frame = wire::encode_limit_order(inst, Side::Buy, 150, 10, TimeInForce::GTC, 1001ULL);
    send_all(client_fd, frame);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getStats().events_pushed == 1, "Expected 1 event pushed to queue");
    ASSERT(gateway.getStats().events_processed == 1, "Expected 1 event processed by engine");

    close(client_fd);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 4. Market Order Reaches Engine
// ─────────────────────────────────────────────
TEST(test_market_order_reaches_engine) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");
    engine.addLimitOrder(inst, Side::Sell, 150, 10, TimeInForce::GTC);

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client_fd = connect_client(gateway.getBoundPort());

    auto frame = wire::encode_market_order(inst, Side::Buy, 10, 1002ULL);
    send_all(client_fd, frame);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getStats().events_pushed == 1, "Expected 1 event pushed to queue");
    ASSERT(gateway.getStats().events_processed == 1, "Expected 1 event processed by engine");
    ASSERT(engine.getTotalTrades() == 1, "Expected 1 trade executed");

    close(client_fd);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 5. Cancel Order Reaches Engine
// ─────────────────────────────────────────────
TEST(test_cancel_order_reaches_engine) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");
    OrderId id = engine.addLimitOrder(inst, Side::Buy, 150, 10, TimeInForce::GTC);

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client_fd = connect_client(gateway.getBoundPort());

    auto frame = wire::encode_cancel_order(inst, id, 1003ULL);
    send_all(client_fd, frame);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getStats().events_pushed == 1, "Expected 1 event pushed");
    ASSERT(gateway.getStats().events_processed == 1, "Expected 1 event processed");

    Order out;
    ASSERT(!engine.getOrder(inst, id, out), "Cancelled order should not be in book");

    close(client_fd);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 6. Modify Order Reaches Engine
// ─────────────────────────────────────────────
TEST(test_modify_order_reaches_engine) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");
    OrderId id = engine.addLimitOrder(inst, Side::Buy, 150, 10, TimeInForce::GTC);

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client_fd = connect_client(gateway.getBoundPort());

    auto frame = wire::encode_modify_order(inst, id, 150, 5, 1004ULL);
    send_all(client_fd, frame);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getStats().events_pushed == 1, "Expected 1 event pushed");
    ASSERT(gateway.getStats().events_processed == 1, "Expected 1 event processed");

    Order out;
    ASSERT(engine.getOrder(inst, id, out), "Order should exist in book");
    ASSERT(out.quantity == 5, "Order quantity should be updated to 5");

    close(client_fd);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 7. Fragmented Frame
// ─────────────────────────────────────────────
TEST(test_fragmented_frame) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client_fd = connect_client(gateway.getBoundPort());
    auto frame = wire::encode_limit_order(inst, Side::Buy, 150, 10, TimeInForce::GTC);

    send_all(client_fd, frame.data(), 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT(gateway.getStats().events_pushed == 0, "No event should be pushed before frame is complete");

    send_all(client_fd, frame.data() + 5, frame.size() - 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getStats().events_pushed == 1, "Expected 1 event pushed after full frame received");

    close(client_fd);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 8. One-Byte-At-A-Time Delivery
// ─────────────────────────────────────────────
TEST(test_one_byte_at_a_time_frame) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client_fd = connect_client(gateway.getBoundPort());
    auto frame = wire::encode_limit_order(inst, Side::Buy, 150, 10, TimeInForce::GTC);

    for (size_t i = 0; i < frame.size(); ++i) {
        send_all(client_fd, &frame[i], 1);
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT(gateway.getStats().events_pushed == 1, "Expected 1 event pushed after 1-byte streaming");

    close(client_fd);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 9. Multiple Frames In One Send
// ─────────────────────────────────────────────
TEST(test_multiple_frames_in_one_send) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client_fd = connect_client(gateway.getBoundPort());

    std::vector<uint8_t> batch;
    for (int i = 0; i < 5; ++i) {
        auto frame = wire::encode_limit_order(inst, Side::Buy, 100 + i, 10, TimeInForce::GTC);
        batch.insert(batch.end(), frame.begin(), frame.end());
    }

    send_all(client_fd, batch);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getStats().events_pushed == 5, "Expected 5 events pushed from batched send");

    close(client_fd);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 10. Multiple Frames with Partial Final Frame
// ─────────────────────────────────────────────
TEST(test_multiple_frames_with_partial_final_frame) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client_fd = connect_client(gateway.getBoundPort());

    auto f1 = wire::encode_limit_order(inst, Side::Buy, 100, 10, TimeInForce::GTC);
    auto f2 = wire::encode_limit_order(inst, Side::Buy, 101, 10, TimeInForce::GTC);
    auto f3 = wire::encode_limit_order(inst, Side::Buy, 102, 10, TimeInForce::GTC);

    std::vector<uint8_t> batch;
    batch.insert(batch.end(), f1.begin(), f1.end());
    batch.insert(batch.end(), f2.begin(), f2.end());
    batch.insert(batch.end(), f3.begin(), f3.begin() + 5);

    send_all(client_fd, batch);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    ASSERT(gateway.getStats().events_pushed == 2, "Expected 2 events pushed so far");

    send_all(client_fd, f3.data() + 5, f3.size() - 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getStats().events_pushed == 3, "Expected 3 events pushed after completion");

    close(client_fd);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 11. Malformed Frame
// ─────────────────────────────────────────────
TEST(test_malformed_frame) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client_fd = connect_client(gateway.getBoundPort());

    auto frame = wire::encode_limit_order(1, Side::Buy, 100, 10, TimeInForce::GTC);
    frame[7] = 5; // Invalid side

    send_all(client_fd, frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getStats().malformed_frames == 1, "Expected 1 malformed frame error");
    ASSERT(gateway.getStats().connections_closed >= 1, "Client should be closed on malformed frame");

    close(client_fd);
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

    int client_fd = connect_client(gateway.getBoundPort());

    std::vector<uint8_t> frame(wire::HEADER_SIZE + 150, 0);
    wire::write_u16_be(&frame[0], 150);
    frame[2] = static_cast<uint8_t>(wire::MessageType::NewLimitOrder);

    send_all(client_fd, frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getStats().malformed_frames == 1, "Oversized frame must count as malformed");

    close(client_fd);
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

    int client_fd = connect_client(gateway.getBoundPort());

    std::vector<uint8_t> frame(wire::HEADER_SIZE + 10, 0);
    wire::write_u16_be(&frame[0], 10);
    frame[2] = 0xAA;

    send_all(client_fd, frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getStats().malformed_frames == 1, "Unknown msg type must count as malformed");

    close(client_fd);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 14. Client Disconnect Handling
// ─────────────────────────────────────────────
TEST(test_client_disconnect_handling) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int c1 = connect_client(gateway.getBoundPort());
    int c2 = connect_client(gateway.getBoundPort());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    ASSERT(gateway.getClientCount() == 2, "Expected 2 clients");

    close(c1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getClientCount() == 1, "Expected 1 client after c1 closes");

    close(c2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getClientCount() == 0, "Expected 0 clients after c2 closes");

    gateway.stop();
}

// ─────────────────────────────────────────────
// 15. Disconnect During Partial Frame
// ─────────────────────────────────────────────
TEST(test_disconnect_during_partial_frame) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int c1 = connect_client(gateway.getBoundPort());
    auto frame = wire::encode_limit_order(inst, Side::Buy, 100, 10, TimeInForce::GTC);

    send_all(c1, frame.data(), 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    close(c1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int c2 = connect_client(gateway.getBoundPort());
    send_all(c2, frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getStats().events_pushed == 1, "Expected 1 event from clean c2 client");

    close(c2);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 16. Multiple Simultaneous Clients
// ─────────────────────────────────────────────
TEST(test_multiple_simultaneous_clients) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    constexpr int NUM_CLIENTS = 4;
    int fds[NUM_CLIENTS];
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        fds[i] = connect_client(gateway.getBoundPort());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ASSERT(gateway.getClientCount() == NUM_CLIENTS, "All clients should be connected");

    for (int i = 0; i < NUM_CLIENTS; ++i) {
        auto frame = wire::encode_limit_order(inst, Side::Buy, 100 + i, 10, TimeInForce::GTC);
        send_all(fds[i], frame);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    ASSERT(gateway.getStats().events_pushed == NUM_CLIENTS, "All orders should be pushed");

    for (int i = 0; i < NUM_CLIENTS; ++i) {
        close(fds[i]);
    }
    gateway.stop();
}

// ─────────────────────────────────────────────
// 17. Queue Full Behavior
// ─────────────────────────────────────────────
TEST(test_queue_full_behavior) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    // Very small queue of 4 slots
    SPSCQueue queue(4);
    GatewayConfig config;
    config.port = 0;
    config.max_backpressure_retries = 2;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    std::vector<uint8_t> batch;
    for (int i = 0; i < 20; ++i) {
        auto f = wire::encode_limit_order(inst, Side::Buy, 100 + i, 1, TimeInForce::GTC);
        batch.insert(batch.end(), f.begin(), f.end());
    }

    send_all(client, batch);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT(gateway.getStats().events_pushed > 0, "Some events should have been pushed");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 18. Buffer Overflow Protection
// ─────────────────────────────────────────────
TEST(test_buffer_overflow_protection) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;
    config.max_client_buffer = 100;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    std::vector<uint8_t> garbage(150, 0x01);
    send_all(client, garbage);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT(gateway.getStats().buffer_overflows == 1, "Expected 1 buffer overflow");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 19. Ping / Pong Over TCP
// ─────────────────────────────────────────────
TEST(test_ping_pong_over_tcp) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    auto ping_frame = wire::encode_ping(0xDEADBEEFCAFEULL);
    send_all(client, ping_frame);

    auto [type, payload] = recv_response(client);
    ASSERT(type == wire::MessageType::Pong, "Expected Pong response");
    ASSERT(payload.size() == 8, "Expected 8 bytes payload");
    ASSERT(wire::read_u64_be(payload.data()) == 0xDEADBEEFCAFEULL, "Nonce must match");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 20. Query Book Over TCP
// ─────────────────────────────────────────────
TEST(test_query_book_over_tcp) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    ReadModel read_model(100, 100);
    read_model.registerSymbol(aapl, "AAPL");

    events::OutboundEvent evt;
    evt.type = events::OutboundEventType::L2Update;
    evt.l2.instrument_id = aapl;
    evt.l2.sequence = 1;
    evt.l2.bid_count = 1;
    evt.l2.ask_count = 1;
    evt.l2.bids[0] = {150, 10};
    evt.l2.asks[0] = {155, 20};
    read_model.applyEvent(evt);

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config, &read_model);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    auto q_frame = wire::encode_query_book(aapl);
    send_all(client, q_frame);

    auto [type, payload] = recv_response(client);
    ASSERT(type == wire::MessageType::QueryBookResponse, "Expected QueryBookResponse");
    ASSERT(payload.size() == 22 + 16, "Expected 38 bytes payload");
    ASSERT(wire::read_u32_be(payload.data()) == aapl, "Instrument ID match");
    ASSERT(wire::read_u64_be(payload.data() + 4) == 1, "Sequence match");
    ASSERT(payload[20] == 1, "Bid count 1");
    ASSERT(payload[21] == 1, "Ask count 1");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 21. Query Trades Over TCP
// ─────────────────────────────────────────────
TEST(test_query_trades_over_tcp) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    ReadModel read_model(100, 100);
    read_model.registerSymbol(aapl, "AAPL");

    events::OutboundEvent evt;
    evt.type = events::OutboundEventType::Trade;
    evt.trade.trade_id = 42;
    evt.trade.instrument_id = aapl;
    evt.trade.buy_order_id = 1;
    evt.trade.sell_order_id = 2;
    evt.trade.price = 150;
    evt.trade.quantity = 10;
    evt.trade.aggressor_side = Side::Buy;
    evt.trade.timestamp = 12345;
    evt.trade.sequence = 10;
    read_model.applyEvent(evt);

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config, &read_model);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    auto q_frame = wire::encode_query_trades(aapl, 10);
    send_all(client, q_frame);

    auto [type, payload] = recv_response(client);
    ASSERT(type == wire::MessageType::QueryTradesResponse, "Expected QueryTradesResponse");
    ASSERT(wire::read_u32_be(payload.data()) == aapl, "Instrument match");
    ASSERT(wire::read_u16_be(payload.data() + 4) == 1, "Trade count 1");
    ASSERT(wire::read_u64_be(payload.data() + 6) == 42, "Trade ID match");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 22. Query Order Over TCP (By Order ID and Client Correlation ID)
// ─────────────────────────────────────────────
TEST(test_query_order_over_tcp) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    ReadModel read_model(100, 100);
    read_model.registerSymbol(aapl, "AAPL");

    events::OutboundEvent evt;
    evt.type = events::OutboundEventType::OrderState;
    evt.order.order_id = 99;
    evt.order.client_order_id = 8888ULL;
    evt.order.instrument_id = aapl;
    evt.order.side = Side::Buy;
    evt.order.price = 150;
    evt.order.original_qty = 20;
    evt.order.remaining_qty = 5;
    evt.order.filled_qty = 15;
    evt.order.status = events::OrderStatus::PartiallyFilled;
    evt.order.reject_code = events::RejectCode::None;
    evt.order.timestamp = 9999;
    evt.order.sequence = 25;
    read_model.applyEvent(evt);

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config, &read_model);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    // 1. Query by order_id 99
    auto q_frame = wire::encode_query_order(99);
    send_all(client, q_frame);

    auto [type1, p1] = recv_response(client);
    ASSERT(type1 == wire::MessageType::QueryOrderResponse, "Expected QueryOrderResponse");
    ASSERT(p1[0] == 1, "Found = 1");
    ASSERT(wire::read_u64_be(p1.data() + 1) == 99, "Order ID 99");
    ASSERT(wire::read_u64_be(p1.data() + 9) == 8888ULL, "Client Order ID 8888");
    ASSERT(wire::read_u32_be(p1.data() + 17) == aapl, "Instrument ID match");
    ASSERT(p1[38] == static_cast<uint8_t>(events::OrderStatus::PartiallyFilled), "Status match");

    // 2. Query by client correlation ID 8888
    auto q_cl_frame = wire::encode_query_order(8888ULL, true);
    send_all(client, q_cl_frame);

    auto [type2, p2] = recv_response(client);
    ASSERT(type2 == wire::MessageType::QueryOrderResponse, "Expected QueryOrderResponse");
    ASSERT(p2[0] == 1, "Found = 1");
    ASSERT(wire::read_u64_be(p2.data() + 1) == 99, "Order ID 99");
    ASSERT(wire::read_u64_be(p2.data() + 9) == 8888ULL, "Client Order ID 8888");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 23. Query Stats Over TCP
// ─────────────────────────────────────────────
TEST(test_query_stats_over_tcp) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    ReadModel read_model(100, 100);
    read_model.registerSymbol(aapl, "AAPL");

    events::OutboundEvent evt;
    evt.type = events::OutboundEventType::Trade;
    evt.trade.trade_id = 1;
    evt.trade.instrument_id = aapl;
    evt.trade.price = 150;
    evt.trade.quantity = 100;
    evt.trade.sequence = 1;
    read_model.applyEvent(evt);

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config, &read_model);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    auto stats_frame = wire::encode_query_stats();
    send_all(client, stats_frame);

    auto [type, payload] = recv_response(client);
    ASSERT(type == wire::MessageType::QueryStatsResponse, "Expected QueryStatsResponse");
    ASSERT(wire::read_u64_be(payload.data()) == 1, "Total trades = 1");
    ASSERT(wire::read_u64_be(payload.data() + 8) == 100, "Total volume = 100");

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
    RUN(test_ping_pong_over_tcp);
    RUN(test_query_book_over_tcp);
    RUN(test_query_trades_over_tcp);
    RUN(test_query_order_over_tcp);
    RUN(test_query_stats_over_tcp);

    std::cout << "\n=======================================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n\n";

    return (failed == 0) ? 0 : 1;
}
