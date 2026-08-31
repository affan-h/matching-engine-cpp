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
#include "projector.h"

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
// 24. Shutdown with Active Connected Clients
// ─────────────────────────────────────────────
TEST(test_shutdown_with_active_connected_clients) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int c1 = connect_client(gateway.getBoundPort());
    int c2 = connect_client(gateway.getBoundPort());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    ASSERT(gateway.getClientCount() == 2, "Expected 2 connected clients");

    // Immediate shutdown while clients remain connected
    gateway.stop();

    ASSERT(!gateway.isRunning(), "Gateway must report not running");
    ASSERT(gateway.getClientCount() == 0, "All clients must be closed");

    close(c1);
    close(c2);
}

// ─────────────────────────────────────────────
// 25. Shutdown with Partial Frame In-Flight
// ─────────────────────────────────────────────
TEST(test_shutdown_with_partial_frame) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int c1 = connect_client(gateway.getBoundPort());
    auto frame = wire::encode_limit_order(aapl, Side::Buy, 150, 10, TimeInForce::GTC);

    // Send only 5 bytes of 17
    send_all(c1, frame.data(), 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Shutdown with partial frame in buffer
    gateway.stop();

    ASSERT(!gateway.isRunning(), "Gateway must report stopped");
    close(c1);
}

// ─────────────────────────────────────────────
// 26. Shutdown Drains Pending Queued Commands
// ─────────────────────────────────────────────
TEST(test_shutdown_with_pending_queued_commands) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    // Push 50 limit orders
    std::vector<uint8_t> batch;
    for (int i = 0; i < 50; ++i) {
        auto f = wire::encode_limit_order(aapl, Side::Buy, 100 + (i % 10), 1, TimeInForce::GTC, 1000 + i);
        batch.insert(batch.end(), f.begin(), f.end());
    }
    send_all(client, batch);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Stop gateway immediately
    gateway.stop();

    // Verify all 50 events were drained and processed by engine
    ASSERT(gateway.getStats().events_processed == 50, "Expected all 50 queued events to be processed during drain");

    close(client);
}

// ─────────────────────────────────────────────
// 27. Shutdown Idempotency
// ─────────────────────────────────────────────
TEST(test_shutdown_idempotency) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    // Call stop multiple times sequentially
    gateway.stop();
    gateway.stop();
    gateway.stop();

    ASSERT(!gateway.isRunning(), "Gateway should remain cleanly stopped");
}

// ─────────────────────────────────────────────
// 28. Rapid Reconnect Burst
// ─────────────────────────────────────────────
TEST(test_rapid_reconnect_burst) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    for (int i = 0; i < 20; ++i) {
        int client = connect_client(gateway.getBoundPort());
        auto ping = wire::encode_ping(i);
        send_all(client, ping);
        auto [type, payload] = recv_response(client);
        ASSERT(type == wire::MessageType::Pong, "Expected Pong response");
        ASSERT(wire::read_u64_be(payload.data()) == static_cast<uint64_t>(i), "Nonce match");
        close(client);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ASSERT(gateway.getClientCount() == 0, "All clients should be cleanly disconnected");

    gateway.stop();
}

// ─────────────────────────────────────────────
// 29. Deep Book Query Over TCP
// ─────────────────────────────────────────────
TEST(test_query_book_deep_levels_over_tcp) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    ReadModel read_model(100, 100);
    read_model.registerSymbol(aapl, "AAPL");

    // Populate 10 bids and 10 asks in read model
    events::OutboundEvent evt;
    evt.type = events::OutboundEventType::L2Update;
    evt.l2.instrument_id = aapl;
    evt.l2.timestamp = 1700000000;
    evt.l2.sequence = 100;
    evt.l2.bid_count = 10;
    evt.l2.ask_count = 10;
    for (int i = 0; i < 10; ++i) {
        evt.l2.bids[i] = {100 - i, static_cast<Quantity>((i + 1) * 10)};
        evt.l2.asks[i] = {101 + i, static_cast<Quantity>((i + 1) * 10)};
    }
    read_model.applyEvent(evt);

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config, &read_model);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());
    auto req = wire::encode_query_book(aapl);
    send_all(client, req);

    auto [type, payload] = recv_response(client);
    ASSERT(type == wire::MessageType::QueryBookResponse, "Expected QueryBookResponse");
    ASSERT(payload.size() == (4 + 8 + 8 + 1 + 1 + (10 + 10) * 8), "Expected payload length for 10 bids and 10 asks");
    ASSERT(payload[20] == 10, "10 bids");
    ASSERT(payload[21] == 10, "10 asks");

    close(client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 30. Gateway Start/Stop/Restart Lifecycle Cycle
// ─────────────────────────────────────────────
TEST(test_gateway_start_stop_restart_cycle) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);

    for (int cycle = 0; cycle < 3; ++cycle) {
        ASSERT(gateway.start(), "Gateway must start cleanly");
        ASSERT(gateway.isRunning(), "Gateway isRunning must be true");

        int port = gateway.getBoundPort();
        int client = connect_client(port);
        ASSERT(client >= 0, "Client must connect");

        auto frame = wire::encode_limit_order(aapl, Side::Buy, 150, 10, TimeInForce::GTC, 1000 + cycle);
        send_all(client, frame);
        close(client);

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        gateway.stop();
        ASSERT(!gateway.isRunning(), "Gateway isRunning must be false after stop");
    }
}

// ─────────────────────────────────────────────
// 31. Sustained Concurrent Client Load
// ─────────────────────────────────────────────
TEST(test_gateway_sustained_concurrent_client_load) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    ReadModel read_model(1000, 1000);
    read_model.registerSymbol(aapl, "AAPL");

    SPSCQueue queue(65536);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config, &read_model);
    gateway.start();

    constexpr int NUM_THREADS = 8;
    constexpr int ORDERS_PER_THREAD = 100;
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&gateway, aapl, t]() {
            int client = connect_client(gateway.getBoundPort());
            if (client < 0) return;

            for (int i = 0; i < ORDERS_PER_THREAD; ++i) {
                uint64_t cl_id = static_cast<uint64_t>(t * 10000 + i + 1);
                auto frame = (i % 2 == 0)
                    ? wire::encode_limit_order(aapl, Side::Buy, 100 + (i % 10), 5, TimeInForce::GTC, cl_id)
                    : wire::encode_limit_order(aapl, Side::Sell, 100 + (i % 10), 5, TimeInForce::GTC, cl_id);
                send_all(client, frame);

                if (i % 25 == 0) {
                    auto ping = wire::encode_ping(cl_id);
                    send_all(client, ping);
                    recv_response(client);
                }
            }
            close(client);
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Wait for consumer to process all events
    while (gateway.getStats().events_processed.load() < (NUM_THREADS * ORDERS_PER_THREAD)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT(gateway.getStats().events_processed.load() >= (NUM_THREADS * ORDERS_PER_THREAD), "All orders processed");
    gateway.stop();
}

// ─────────────────────────────────────────────
// 32. Adversarial Disconnect Burst During Active Traffic
// ─────────────────────────────────────────────
TEST(test_gateway_adversarial_disconnect_burst_during_active_traffic) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    gateway.start();

    constexpr int NUM_BURSTS = 20;
    std::vector<std::thread> threads;

    for (int i = 0; i < NUM_BURSTS; ++i) {
        threads.emplace_back([&gateway, aapl, i]() {
            int client = connect_client(gateway.getBoundPort());
            if (client < 0) return;

            auto frame = wire::encode_limit_order(aapl, Side::Buy, 100, 10, TimeInForce::GTC, i + 1);
            // Send partial frame (first 5 bytes of 27-byte frame)
            if (frame.size() > 5) {
                ::send(client, frame.data(), 5, 0);
            }
            // Abrupt disconnect
            close(client);
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Send a valid order from a healthy client to verify gateway remains operational
    int health_client = connect_client(gateway.getBoundPort());
    ASSERT(health_client >= 0, "Healthy client must connect after disconnect burst");
    auto ping = wire::encode_ping(9999);
    send_all(health_client, ping);
    auto [type, payload] = recv_response(health_client);
    ASSERT(type == wire::MessageType::Pong, "Gateway must remain responsive");
    close(health_client);

    gateway.stop();
}

// ─────────────────────────────────────────────
// 33. Invalid Configuration Rejection
// ─────────────────────────────────────────────
TEST(test_gateway_invalid_configuration_rejection) {
    MatchingEngine engine;
    SPSCQueue queue(1024);

    // Port out of bounds
    GatewayConfig cfg1;
    cfg1.port = 99999;
    TcpGateway gw1(engine, queue, cfg1);
    ASSERT(!gw1.start(), "Gateway must fail to start on invalid port 99999");
    ASSERT(!gw1.isRunning(), "Gateway must not be running");

    // Buffer too small (< 256)
    GatewayConfig cfg2;
    cfg2.port = 0;
    cfg2.max_client_buffer = 50;
    TcpGateway gw2(engine, queue, cfg2);
    ASSERT(!gw2.start(), "Gateway must fail on buffer < 256");

    // Backpressure retries 0
    GatewayConfig cfg3;
    cfg3.port = 0;
    cfg3.max_backpressure_retries = 0;
    TcpGateway gw3(engine, queue, cfg3);
    ASSERT(!gw3.start(), "Gateway must fail on retries 0");

    // Max connections 0
    GatewayConfig cfg4;
    cfg4.port = 0;
    cfg4.max_connections = 0;
    TcpGateway gw4(engine, queue, cfg4);
    ASSERT(!gw4.start(), "Gateway must fail on max_connections 0");
}

// ─────────────────────────────────────────────
// 34. Max Connections Limit Enforcement
// ─────────────────────────────────────────────
TEST(test_gateway_max_connections_limit_enforcement) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;
    config.max_connections = 2; // Limit to 2 concurrent clients

    TcpGateway gateway(engine, queue, config);
    ASSERT(gateway.start(), "Gateway starts with max_connections=2");

    int port = gateway.getBoundPort();
    int c1 = connect_client(port);
    int c2 = connect_client(port);
    ASSERT(c1 >= 0 && c2 >= 0, "First 2 clients connect successfully");

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT(gateway.getClientCount() == 2, "Active clients should be 2");

    // 3rd client exceeds limit
    int c3 = connect_client(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Attempting to send on c3 should fail or encounter EOF
    auto ping = wire::encode_ping(1);
    ssize_t sent = ::send(c3, ping.data(), ping.size(), 0);
    (void)sent;

    uint8_t dummy[16];
    ssize_t recvd = ::recv(c3, dummy, sizeof(dummy), 0);
    ASSERT(recvd <= 0, "3rd connection must be closed by gateway due to max_connections limit");
    ASSERT(gateway.getStats().connections_rejected.load() >= 1, "Expected connections_rejected to be incremented");

    // Verify first 2 clients are still perfectly functional
    auto ping_c1 = wire::encode_ping(100);
    send_all(c1, ping_c1);
    auto [type1, p1] = recv_response(c1);
    ASSERT(type1 == wire::MessageType::Pong, "Client 1 still responsive");

    close(c1);
    close(c2);
    close(c3);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 35. Gateway Operational Statistics Accounting
// ─────────────────────────────────────────────
TEST(test_gateway_operational_statistics_accounting) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    ReadModel read_model(100, 100);
    read_model.registerSymbol(aapl, "AAPL");

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config, &read_model);
    gateway.start();

    int client = connect_client(gateway.getBoundPort());

    // 1. Session frame (Ping)
    auto ping = wire::encode_ping(42);
    send_all(client, ping);
    recv_response(client);

    // 2. Command frame (Limit order)
    auto order = wire::encode_limit_order(aapl, Side::Buy, 150, 10, TimeInForce::GTC, 999ULL);
    send_all(client, order);

    // 3. Query frame (Query stats)
    auto qstats = wire::encode_query_stats();
    send_all(client, qstats);
    recv_response(client);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    const auto& stats = gateway.getStats();
    ASSERT(stats.connections_accepted.load() == 1, "1 connection accepted");
    ASSERT(stats.session_frames_processed.load() == 1, "1 session frame");
    ASSERT(stats.events_pushed.load() == 1, "1 command pushed");
    ASSERT(stats.events_processed.load() == 1, "1 command processed");
    ASSERT(stats.queries_processed.load() == 1, "1 query processed");

    close(client);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT(stats.connections_closed.load() == 1, "1 connection closed");

    gateway.stop();
}

// ─────────────────────────────────────────────
// 36. Malformed Frame Followed by Clean Reconnect
// ─────────────────────────────────────────────
TEST(test_gateway_malformed_traffic_followed_by_clean_reconnect) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    ReadModel read_model(100, 100);
    read_model.registerSymbol(aapl, "AAPL");

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config, &read_model);
    gateway.start();

    // 1. Client 1 sends completely invalid garbage -> triggers disconnect
    int c1 = connect_client(gateway.getBoundPort());
    ASSERT(c1 >= 0, "Client 1 connects");
    std::vector<uint8_t> garbage = {0xFF, 0xFF, 0x00, 0x12, 0x34};
    send_all(c1, garbage);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    close(c1);

    ASSERT(gateway.getStats().malformed_frames.load() >= 1, "Malformed frame recorded");

    // 2. Client 2 reconnects and executes a valid Ping and Limit Order
    int c2 = connect_client(gateway.getBoundPort());
    ASSERT(c2 >= 0, "Client 2 reconnects cleanly");

    auto ping = wire::encode_ping(777);
    send_all(c2, ping);
    auto [type, p_ping] = recv_response(c2);
    ASSERT(type == wire::MessageType::Pong, "Client 2 receives Pong");
    ASSERT(wire::read_u64_be(p_ping.data()) == 777, "Nonce matches");

    auto order = wire::encode_limit_order(aapl, Side::Buy, 150, 10, TimeInForce::GTC, 12345ULL);
    send_all(c2, order);

    close(c2);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 37. Gateway Liveness and Health Semantics
// ─────────────────────────────────────────────
TEST(test_gateway_liveness_and_worker_health_semantics) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config);
    ASSERT(!gateway.isRunning(), "Not running before start");
    ASSERT(!gateway.isHealthy(), "Not healthy before start");

    ASSERT(gateway.start(), "Start succeeds");
    ASSERT(gateway.isRunning(), "Running after start");
    ASSERT(gateway.isHealthy(), "Healthy after start");

    gateway.stop();
    ASSERT(!gateway.isRunning(), "Not running after stop");
    ASSERT(!gateway.isHealthy(), "Not healthy after stop");
}

// ─────────────────────────────────────────────
// 38. Connection Saturation and Recovery
// ─────────────────────────────────────────────
TEST(test_gateway_connection_saturation_and_recovery) {
    MatchingEngine engine;
    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;
    config.max_connections = 3;

    TcpGateway gateway(engine, queue, config);
    ASSERT(gateway.start(), "Gateway starts");

    int port = gateway.getBoundPort();
    int c1 = connect_client(port);
    int c2 = connect_client(port);
    int c3 = connect_client(port);
    ASSERT(c1 >= 0 && c2 >= 0 && c3 >= 0, "3 connections opened");

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT(gateway.getClientCount() == 3, "3 active clients");

    // 4th connection is rejected
    int c4 = connect_client(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    uint8_t buf[8];
    ssize_t r = ::recv(c4, buf, sizeof(buf), 0);
    ASSERT(r <= 0, "4th connection rejected");
    close(c4);

    // Close c1 and c2 -> active count drops to 1
    close(c1);
    close(c2);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ASSERT(gateway.getClientCount() == 1, "Active clients dropped to 1");

    // Now a new client connects successfully
    int c5 = connect_client(port);
    ASSERT(c5 >= 0, "New client connects after capacity freed");
    auto ping = wire::encode_ping(999);
    send_all(c5, ping);
    auto [t, p] = recv_response(c5);
    ASSERT(t == wire::MessageType::Pong, "New client functional");

    close(c3);
    close(c5);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 39. Sustained Backpressure and Drain Under High Throughput
// ─────────────────────────────────────────────
TEST(test_gateway_sustained_backpressure_and_drain_under_high_throughput) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    InstrumentId rel  = engine.registerInstrument("RELIANCE");
    ReadModel read_model(10000, 10000);
    read_model.registerSymbol(aapl, "AAPL");
    read_model.registerSymbol(rel, "RELIANCE");

    OutboundEventQueue outbound_queue(65536);
    engine.setOutboundQueue(&outbound_queue);

    Projector projector(outbound_queue, read_model);
    projector.start();

    SPSCQueue command_queue(65536);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, command_queue, config, &read_model);
    gateway.start();

    constexpr int NUM_CLIENTS = 8;
    constexpr int ORDERS_PER_CLIENT = 200;
    std::vector<std::thread> workers;

    for (int c = 0; c < NUM_CLIENTS; ++c) {
        workers.emplace_back([&gateway, aapl, rel, c]() {
            int client = connect_client(gateway.getBoundPort());
            if (client < 0) return;

            for (int i = 0; i < ORDERS_PER_CLIENT; ++i) {
                uint64_t cl_id = static_cast<uint64_t>(c * 10000 + i + 1);
                InstrumentId inst = (i % 2 == 0) ? aapl : rel;
                Price px = 100 + (i % 20);
                auto frame = wire::encode_limit_order(inst, (i % 4 < 2) ? Side::Buy : Side::Sell, px, 5, TimeInForce::GTC, cl_id);
                send_all(client, frame);
            }
            close(client);
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    // Wait until all commands are processed through the engine and projector
    while (gateway.getStats().events_processed.load() < static_cast<uint64_t>(NUM_CLIENTS * ORDERS_PER_CLIENT)) {
        std::this_thread::yield();
    }

    gateway.stop();
    projector.stop();

    ASSERT(gateway.getStats().events_processed.load() == static_cast<uint64_t>(NUM_CLIENTS * ORDERS_PER_CLIENT), "All client events processed");
    ASSERT(read_model.getLastSequence() > 0, "ReadModel sequence advanced");
}

// ─────────────────────────────────────────────
// 40. Adversarial Parser Fuzzing and Recovery
// ─────────────────────────────────────────────
TEST(test_gateway_adversarial_parser_fuzzing_and_recovery) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    ReadModel read_model(100, 100);
    read_model.registerSymbol(aapl, "AAPL");

    SPSCQueue queue(1024);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, queue, config, &read_model);
    gateway.start();

    // Adversarial payloads: invalid types, oversized lengths, bad prices/quantities, truncated buffers
    std::vector<std::vector<uint8_t>> hostile_payloads = {
        {0x00, 0xFF, 0x01},                         // Claim 255 bytes payload (> MAX_PAYLOAD_LENGTH 128)
        {0x00, 0x05, 0x99, 0x01, 0x02, 0x03, 0x04}, // Unknown message type 0x99
        {0x00, 0x16, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0A, 0x00}, // Invalid side 5
        {0x00, 0x16, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00}, // Invalid price 0
        {0x00, 0x16, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00}, // Invalid quantity 0
        {0x00, 0x16, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x0A, 0x09}, // Invalid TIF 9
    };

    for (const auto& hostile : hostile_payloads) {
        int client = connect_client(gateway.getBoundPort());
        ASSERT(client >= 0, "Attacker client connects");
        send_all(client, hostile);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        close(client);
    }

    ASSERT(gateway.getStats().malformed_frames.load() >= hostile_payloads.size(), "All hostile frames flagged");

    // Legitimate client connects and performs normal operations cleanly
    int good_client = connect_client(gateway.getBoundPort());
    ASSERT(good_client >= 0, "Good client connects cleanly");

    auto ping = wire::encode_ping(8888);
    send_all(good_client, ping);
    auto [type, payload] = recv_response(good_client);
    ASSERT(type == wire::MessageType::Pong, "Good client receives Pong");
    ASSERT(wire::read_u64_be(payload.data()) == 8888, "Good client nonce matches");

    close(good_client);
    gateway.stop();
}

// ─────────────────────────────────────────────
// 41. Large Burst Concurrent Queries and Commands
// ─────────────────────────────────────────────
TEST(test_gateway_large_burst_concurrent_queries_and_commands) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    ReadModel read_model(1000, 1000);
    read_model.registerSymbol(aapl, "AAPL");

    OutboundEventQueue outbound_queue(4096);
    engine.setOutboundQueue(&outbound_queue);

    Projector projector(outbound_queue, read_model);
    projector.start();

    SPSCQueue command_queue(4096);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, command_queue, config, &read_model);
    gateway.start();

    constexpr int NUM_QUERY_THREADS = 6;
    constexpr int NUM_ORDER_THREADS = 6;
    constexpr int OPS_PER_THREAD = 100;

    std::vector<std::thread> threads;

    // Launch query threads
    for (int t = 0; t < NUM_QUERY_THREADS; ++t) {
        threads.emplace_back([&gateway, aapl]() {
            int client = connect_client(gateway.getBoundPort());
            if (client < 0) return;

            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                if (i % 3 == 0) {
                    auto req = wire::encode_query_book(aapl);
                    send_all(client, req);
                    recv_response(client);
                } else if (i % 3 == 1) {
                    auto req = wire::encode_query_trades(aapl, 10);
                    send_all(client, req);
                    recv_response(client);
                } else {
                    auto req = wire::encode_query_stats();
                    send_all(client, req);
                    recv_response(client);
                }
            }
            close(client);
        });
    }

    // Launch order command threads
    for (int t = 0; t < NUM_ORDER_THREADS; ++t) {
        threads.emplace_back([&gateway, aapl, t]() {
            int client = connect_client(gateway.getBoundPort());
            if (client < 0) return;

            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                uint64_t cl_id = static_cast<uint64_t>(t * 1000 + i + 1);
                auto frame = wire::encode_limit_order(aapl, (i % 2 == 0) ? Side::Buy : Side::Sell, 150, 1, TimeInForce::GTC, cl_id);
                send_all(client, frame);
            }
            close(client);
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Drain
    while (gateway.getStats().events_processed.load() < static_cast<uint64_t>(NUM_ORDER_THREADS * OPS_PER_THREAD)) {
        std::this_thread::yield();
    }

    gateway.stop();
    projector.stop();

    ASSERT(gateway.getStats().queries_processed.load() >= static_cast<uint64_t>(NUM_QUERY_THREADS * OPS_PER_THREAD), "All queries completed");
    ASSERT(gateway.getStats().events_processed.load() == static_cast<uint64_t>(NUM_ORDER_THREADS * OPS_PER_THREAD), "All commands completed");
}

// ─────────────────────────────────────────────
// 42. Clean Graceful Shutdown Under Intense Traffic Burst
// ─────────────────────────────────────────────
TEST(test_gateway_clean_graceful_shutdown_under_intense_traffic_burst) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    ReadModel read_model(10000, 10000);
    read_model.registerSymbol(aapl, "AAPL");

    OutboundEventQueue outbound_queue(16384);
    engine.setOutboundQueue(&outbound_queue);

    Projector projector(outbound_queue, read_model);
    projector.start();

    SPSCQueue command_queue(16384);
    GatewayConfig config;
    config.port = 0;

    TcpGateway gateway(engine, command_queue, config, &read_model);
    gateway.start();

    std::atomic<bool> stop_traffic{false};
    constexpr int NUM_THREADS = 6;
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&gateway, aapl, &stop_traffic, t]() {
            int client = connect_client(gateway.getBoundPort());
            if (client < 0) return;

            uint64_t count = 0;
            while (!stop_traffic.load(std::memory_order_relaxed)) {
                Side side = (count % 2 == 0) ? Side::Buy : Side::Sell;
                auto frame = wire::encode_limit_order(aapl, side, 150, 1, TimeInForce::GTC, t * 100000ULL + (++count));
                try {
                    send_all(client, frame);
                } catch (...) {
                    break;
                }
            }
            close(client);
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Shutdown gateway while clients are actively sending
    gateway.stop();
    stop_traffic.store(true, std::memory_order_relaxed);

    for (auto& th : threads) {
        th.join();
    }

    projector.stop();

    // Verify all pushed events were completely processed during drain
    ASSERT(gateway.getStats().events_pushed.load() == gateway.getStats().events_processed.load(), "All pushed events drained into engine");
    ASSERT(!gateway.isRunning(), "Gateway is stopped");
    ASSERT(!projector.isRunning(), "Projector is stopped");
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
    RUN(test_shutdown_with_active_connected_clients);
    RUN(test_shutdown_with_partial_frame);
    RUN(test_shutdown_with_pending_queued_commands);
    RUN(test_shutdown_idempotency);
    RUN(test_rapid_reconnect_burst);
    RUN(test_query_book_deep_levels_over_tcp);
    RUN(test_gateway_start_stop_restart_cycle);
    RUN(test_gateway_sustained_concurrent_client_load);
    RUN(test_gateway_adversarial_disconnect_burst_during_active_traffic);
    RUN(test_gateway_invalid_configuration_rejection);
    RUN(test_gateway_max_connections_limit_enforcement);
    RUN(test_gateway_operational_statistics_accounting);
    RUN(test_gateway_malformed_traffic_followed_by_clean_reconnect);
    RUN(test_gateway_liveness_and_worker_health_semantics);
    RUN(test_gateway_connection_saturation_and_recovery);
    RUN(test_gateway_sustained_backpressure_and_drain_under_high_throughput);
    RUN(test_gateway_adversarial_parser_fuzzing_and_recovery);
    RUN(test_gateway_large_burst_concurrent_queries_and_commands);
    RUN(test_gateway_clean_graceful_shutdown_under_intense_traffic_burst);

    std::cout << "\n=======================================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n\n";

    return (failed == 0) ? 0 : 1;
}
