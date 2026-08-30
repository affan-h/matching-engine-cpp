#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "wire_protocol.h"
#include "tcp_parser.h"

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

// ─────────────────────────────────────────────
// 1. Limit Order Round Trip (Legacy & Correlated)
// ─────────────────────────────────────────────
TEST(test_limit_order_round_trip) {
    // Legacy 14B payload
    {
        auto bytes = wire::encode_limit_order(1, Side::Buy, 150, 25, TimeInForce::GTC);
        ASSERT(bytes.size() == 17, "Expected 17-byte limit order frame");

        TcpParser parser;
        parser.append(bytes);

        OrderEvent event;
        ParseError error = ParseError::None;
        ParseStatus status = parser.parseNext(event, error);

        ASSERT(status == ParseStatus::Ok, "Parse status should be Ok");
        ASSERT(error == ParseError::None, "No parse error expected");
        ASSERT(event.type == EventType::LimitOrder, "Expected LimitOrder event type");
        ASSERT(event.instrument == 1, "Expected instrument 1");
        ASSERT(event.id == 0, "Expected order id 0 for new limit order");
        ASSERT(event.client_order_id == 0, "Expected cl_ord_id 0");
        ASSERT(event.side == Side::Buy, "Expected Side::Buy");
        ASSERT(event.price == 150, "Expected price 150");
        ASSERT(event.qty == 25, "Expected quantity 25");
        ASSERT(event.tif == TimeInForce::GTC, "Expected TimeInForce::GTC");
        ASSERT(!parser.hasPartialData(), "Parser should have no remaining bytes");
    }

    // Correlated 22B payload
    {
        auto bytes = wire::encode_limit_order(1, Side::Buy, 150, 25, TimeInForce::GTC, 999001ULL);
        ASSERT(bytes.size() == 25, "Expected 25-byte limit order frame");

        TcpParser parser;
        parser.append(bytes);

        OrderEvent event;
        ParseError error = ParseError::None;
        ParseStatus status = parser.parseNext(event, error);

        ASSERT(status == ParseStatus::Ok, "Parse status should be Ok");
        ASSERT(event.client_order_id == 999001ULL, "Expected cl_ord_id 999001");
        ASSERT(event.price == 150 && event.qty == 25, "Field mismatch");
    }
}

// ─────────────────────────────────────────────
// 2. Market Order Round Trip
// ─────────────────────────────────────────────
TEST(test_market_order_round_trip) {
    auto bytes = wire::encode_market_order(2, Side::Sell, 50, 888002ULL);
    ASSERT(bytes.size() == 20, "Expected 20-byte market order frame with correlation ID");

    TcpParser parser;
    parser.append(bytes);

    OrderEvent event;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNext(event, error);

    ASSERT(status == ParseStatus::Ok, "Parse status should be Ok");
    ASSERT(error == ParseError::None, "No parse error expected");
    ASSERT(event.type == EventType::MarketOrder, "Expected MarketOrder event type");
    ASSERT(event.instrument == 2, "Expected instrument 2");
    ASSERT(event.client_order_id == 888002ULL, "Expected cl_ord_id 888002");
    ASSERT(event.side == Side::Sell, "Expected Side::Sell");
    ASSERT(event.price == 0, "Expected price 0 for market order");
    ASSERT(event.qty == 50, "Expected quantity 50");
    ASSERT(event.tif == TimeInForce::IOC, "Expected TimeInForce::IOC");
    ASSERT(!parser.hasPartialData(), "Parser should have no remaining bytes");
}

// ─────────────────────────────────────────────
// 3. Cancel Order Round Trip
// ─────────────────────────────────────────────
TEST(test_cancel_order_round_trip) {
    auto bytes = wire::encode_cancel_order(1, 123456789ULL, 777003ULL);
    ASSERT(bytes.size() == 23, "Expected 23-byte cancel order frame with correlation ID");

    TcpParser parser;
    parser.append(bytes);

    OrderEvent event;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNext(event, error);

    ASSERT(status == ParseStatus::Ok, "Parse status should be Ok");
    ASSERT(error == ParseError::None, "No parse error expected");
    ASSERT(event.type == EventType::CancelOrder, "Expected CancelOrder event type");
    ASSERT(event.instrument == 1, "Expected instrument 1");
    ASSERT(event.id == 123456789ULL, "Expected order id 123456789");
    ASSERT(event.client_order_id == 777003ULL, "Expected client correlation ID 777003");
    ASSERT(!parser.hasPartialData(), "Parser should have no remaining bytes");
}

// ─────────────────────────────────────────────
// 4. Modify Order Round Trip
// ─────────────────────────────────────────────
TEST(test_modify_order_round_trip) {
    auto bytes = wire::encode_modify_order(3, 987654321ULL, 2800, 15, 666004ULL);
    ASSERT(bytes.size() == 31, "Expected 31-byte modify order frame with correlation ID");

    TcpParser parser;
    parser.append(bytes);

    OrderEvent event;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNext(event, error);

    ASSERT(status == ParseStatus::Ok, "Parse status should be Ok");
    ASSERT(error == ParseError::None, "No parse error expected");
    ASSERT(event.type == EventType::ModifyOrder, "Expected ModifyOrder event type");
    ASSERT(event.instrument == 3, "Expected instrument 3");
    ASSERT(event.id == 987654321ULL, "Expected order id 987654321");
    ASSERT(event.client_order_id == 666004ULL, "Expected client correlation ID 666004");
    ASSERT(event.price == 2800, "Expected new price 2800");
    ASSERT(event.qty == 15, "Expected new quantity 15");
    ASSERT(!parser.hasPartialData(), "Parser should have no remaining bytes");
}

// ─────────────────────────────────────────────
// 5. Partial Header
// ─────────────────────────────────────────────
TEST(test_partial_header) {
    auto bytes = wire::encode_limit_order(1, Side::Buy, 100, 10, TimeInForce::GTC);

    TcpParser parser;
    // Chunk 1: Only 1 byte of header
    parser.append(bytes.data(), 1);

    OrderEvent event;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNext(event, error);
    ASSERT(status == ParseStatus::NeedMoreData, "Expected NeedMoreData after 1 byte");
    ASSERT(parser.hasPartialData(), "Parser should have partial data");
    ASSERT(parser.remainingBytes() == 1, "Expected 1 remaining byte");

    // Chunk 2: Remaining 16 bytes (2 header bytes + 14 payload bytes)
    parser.append(bytes.data() + 1, bytes.size() - 1);
    status = parser.parseNext(event, error);
    ASSERT(status == ParseStatus::Ok, "Expected Ok after completing frame");
    ASSERT(event.price == 100 && event.qty == 10, "Event fields must match");
    ASSERT(!parser.hasPartialData(), "Buffer should be empty");
}

// ─────────────────────────────────────────────
// 6. Partial Payload
// ─────────────────────────────────────────────
TEST(test_partial_payload) {
    auto bytes = wire::encode_limit_order(2, Side::Sell, 500, 30, TimeInForce::IOC);

    TcpParser parser;
    // Chunk 1: 3-byte header + first 5 bytes of payload = 8 bytes total
    parser.append(bytes.data(), 8);

    OrderEvent event;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNext(event, error);
    ASSERT(status == ParseStatus::NeedMoreData, "Expected NeedMoreData for incomplete payload");
    ASSERT(parser.remainingBytes() == 8, "Expected 8 remaining bytes");

    // Chunk 2: Remaining 9 bytes of payload
    parser.append(bytes.data() + 8, bytes.size() - 8);
    status = parser.parseNext(event, error);
    ASSERT(status == ParseStatus::Ok, "Expected Ok after remaining payload arrives");
    ASSERT(event.instrument == 2 && event.price == 500 && event.qty == 30, "Fields must match");
    ASSERT(event.tif == TimeInForce::IOC, "Expected TimeInForce::IOC");
}

// ─────────────────────────────────────────────
// 7. One-Byte-At-A-Time Delivery
// ─────────────────────────────────────────────
TEST(test_one_byte_at_a_time) {
    auto bytes = wire::encode_modify_order(1, 42ULL, 105, 8);
    TcpParser parser;

    OrderEvent event;
    ParseError error = ParseError::None;

    for (size_t i = 0; i < bytes.size() - 1; ++i) {
        parser.append(&bytes[i], 1);
        ParseStatus status = parser.parseNext(event, error);
        ASSERT(status == ParseStatus::NeedMoreData, "Should need more data before final byte");
        ASSERT(parser.remainingBytes() == (i + 1), "Remaining bytes count mismatch");
    }

    // Feed last byte
    parser.append(&bytes.back(), 1);
    ParseStatus status = parser.parseNext(event, error);
    ASSERT(status == ParseStatus::Ok, "Should succeed on final byte");
    ASSERT(event.type == EventType::ModifyOrder, "Expected ModifyOrder");
    ASSERT(event.id == 42ULL, "Expected order id 42");
    ASSERT(event.price == 105 && event.qty == 8, "Expected price 105 and qty 8");
    ASSERT(!parser.hasPartialData(), "Buffer should be empty");
}

// ─────────────────────────────────────────────
// 8. Multiple Frames in One Buffer
// ─────────────────────────────────────────────
TEST(test_multiple_frames_in_one_buffer) {
    auto f1 = wire::encode_limit_order(1, Side::Buy, 100, 10, TimeInForce::GTC);
    auto f2 = wire::encode_market_order(2, Side::Sell, 20);
    auto f3 = wire::encode_cancel_order(1, 99ULL);

    std::vector<uint8_t> combined;
    combined.insert(combined.end(), f1.begin(), f1.end());
    combined.insert(combined.end(), f2.begin(), f2.end());
    combined.insert(combined.end(), f3.begin(), f3.end());

    TcpParser parser;
    parser.append(combined);

    std::vector<OrderEvent> events;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseAll(events, error);

    ASSERT(status == ParseStatus::Ok, "parseAll should succeed");
    ASSERT(events.size() == 3, "Expected 3 parsed events");
    ASSERT(events[0].type == EventType::LimitOrder && events[0].price == 100, "Event 1 mismatch");
    ASSERT(events[1].type == EventType::MarketOrder && events[1].qty == 20, "Event 2 mismatch");
    ASSERT(events[2].type == EventType::CancelOrder && events[2].id == 99ULL, "Event 3 mismatch");
    ASSERT(!parser.hasPartialData(), "Buffer should be completely consumed");
}

// ─────────────────────────────────────────────
// 9. Multiple Frames With Final Partial Frame
// ─────────────────────────────────────────────
TEST(test_multiple_frames_with_final_partial_frame) {
    auto f1 = wire::encode_limit_order(1, Side::Buy, 100, 10, TimeInForce::GTC);
    auto f2 = wire::encode_market_order(2, Side::Sell, 20);
    auto f3 = wire::encode_cancel_order(1, 99ULL);

    std::vector<uint8_t> chunk1;
    chunk1.insert(chunk1.end(), f1.begin(), f1.end());
    chunk1.insert(chunk1.end(), f2.begin(), f2.end());
    chunk1.insert(chunk1.end(), f3.begin(), f3.begin() + 5);

    TcpParser parser;
    parser.append(chunk1);

    OrderEvent event;
    ParseError error = ParseError::None;

    // First frame
    ASSERT(parser.parseNext(event, error) == ParseStatus::Ok, "Expected Ok for frame 1");
    ASSERT(event.type == EventType::LimitOrder, "Expected frame 1 to be LimitOrder");

    // Second frame
    ASSERT(parser.parseNext(event, error) == ParseStatus::Ok, "Expected Ok for frame 2");
    ASSERT(event.type == EventType::MarketOrder, "Expected frame 2 to be MarketOrder");

    // Third frame (partial)
    ASSERT(parser.parseNext(event, error) == ParseStatus::NeedMoreData, "Expected NeedMoreData for frame 3");
    ASSERT(parser.remainingBytes() == 5, "Expected 5 bytes remaining in buffer");

    // Feed remaining bytes of f3
    parser.append(f3.data() + 5, f3.size() - 5);
    ASSERT(parser.parseNext(event, error) == ParseStatus::Ok, "Expected Ok for frame 3 after completion");
    ASSERT(event.type == EventType::CancelOrder && event.id == 99ULL, "Expected frame 3 CancelOrder");
    ASSERT(!parser.hasPartialData(), "Buffer should now be empty");
}

// ─────────────────────────────────────────────
// 10. Wrong Payload Length
// ─────────────────────────────────────────────
TEST(test_wrong_payload_length) {
    // Limit order requires payload length 14 or 22, but declare 13
    std::vector<uint8_t> frame(wire::HEADER_SIZE + 13, 0);
    wire::write_u16_be(&frame[0], 13);
    frame[2] = static_cast<uint8_t>(wire::MessageType::NewLimitOrder);

    TcpParser parser;
    parser.append(frame);

    OrderEvent event;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNext(event, error);

    ASSERT(status == ParseStatus::Error, "Expected ParseStatus::Error for wrong payload length");
    ASSERT(error == ParseError::InvalidPayloadLength, "Expected InvalidPayloadLength error");
}

// ─────────────────────────────────────────────
// 11. Payload > 128 (Oversized Frame)
// ─────────────────────────────────────────────
TEST(test_payload_too_large) {
    std::vector<uint8_t> frame(wire::HEADER_SIZE + 150, 0);
    wire::write_u16_be(&frame[0], 150); // 150 > 128
    frame[2] = static_cast<uint8_t>(wire::MessageType::NewLimitOrder);

    TcpParser parser;
    parser.append(frame);

    OrderEvent event;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNext(event, error);

    ASSERT(status == ParseStatus::Error, "Expected ParseStatus::Error for oversized payload");
    ASSERT(error == ParseError::PayloadTooLarge, "Expected PayloadTooLarge error");
}

// ─────────────────────────────────────────────
// 12. Unknown Message Type
// ─────────────────────────────────────────────
TEST(test_unknown_message_type) {
    std::vector<uint8_t> frame(wire::HEADER_SIZE + 10, 0);
    wire::write_u16_be(&frame[0], 10);
    frame[2] = 0x99; // Invalid message type

    TcpParser parser;
    parser.append(frame);

    OrderEvent event;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNext(event, error);

    ASSERT(status == ParseStatus::Error, "Expected ParseStatus::Error for unknown msg type");
    ASSERT(error == ParseError::UnknownMessageType, "Expected UnknownMessageType error");
}

// ─────────────────────────────────────────────
// 13. Invalid Side
// ─────────────────────────────────────────────
TEST(test_invalid_side) {
    auto frame = wire::encode_limit_order(1, Side::Buy, 100, 10, TimeInForce::GTC);
    frame[7] = 2; // Invalid side (valid is 0 or 1)

    TcpParser parser;
    parser.append(frame);

    OrderEvent event;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNext(event, error);

    ASSERT(status == ParseStatus::Error, "Expected ParseStatus::Error for invalid side");
    ASSERT(error == ParseError::InvalidSide, "Expected InvalidSide error");
}

// ─────────────────────────────────────────────
// 14. Invalid TimeInForce
// ─────────────────────────────────────────────
TEST(test_invalid_tif) {
    auto frame = wire::encode_limit_order(1, Side::Buy, 100, 10, TimeInForce::GTC);
    frame[16] = 5; // Invalid TIF (valid is 0, 1, 2)

    TcpParser parser;
    parser.append(frame);

    OrderEvent event;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNext(event, error);

    ASSERT(status == ParseStatus::Error, "Expected ParseStatus::Error for invalid TIF");
    ASSERT(error == ParseError::InvalidTimeInForce, "Expected InvalidTimeInForce error");
}

// ─────────────────────────────────────────────
// 15. Zero Quantity
// ─────────────────────────────────────────────
TEST(test_zero_quantity) {
    auto frame = wire::encode_limit_order(1, Side::Buy, 100, 0, TimeInForce::GTC);
    TcpParser parser;
    parser.append(frame);

    OrderEvent event;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNext(event, error);

    ASSERT(status == ParseStatus::Error, "Expected ParseStatus::Error for zero quantity");
    ASSERT(error == ParseError::InvalidQuantity, "Expected InvalidQuantity error");
}

// ─────────────────────────────────────────────
// 16. Invalid Price
// ─────────────────────────────────────────────
TEST(test_invalid_price) {
    // Price = 0
    {
        auto frame = wire::encode_limit_order(1, Side::Buy, 0, 10, TimeInForce::GTC);
        TcpParser parser;
        parser.append(frame);

        OrderEvent event;
        ParseError error = ParseError::None;
        ParseStatus status = parser.parseNext(event, error);

        ASSERT(status == ParseStatus::Error, "Expected error for price 0");
        ASSERT(error == ParseError::InvalidPrice, "Expected InvalidPrice error");
    }

    // Price > 100,000 (MAX_PRICE_VAL)
    {
        auto frame = wire::encode_limit_order(1, Side::Buy, 100001, 10, TimeInForce::GTC);
        TcpParser parser;
        parser.append(frame);

        OrderEvent event;
        ParseError error = ParseError::None;
        ParseStatus status = parser.parseNext(event, error);

        ASSERT(status == ParseStatus::Error, "Expected error for price > 100000");
        ASSERT(error == ParseError::InvalidPrice, "Expected InvalidPrice error");
    }
}

// ─────────────────────────────────────────────
// 17. Zero Order ID for Cancel/Modify
// ─────────────────────────────────────────────
TEST(test_zero_order_id) {
    // Cancel with OrderId = 0
    {
        auto frame = wire::encode_cancel_order(1, 0ULL);
        TcpParser parser;
        parser.append(frame);

        OrderEvent event;
        ParseError error = ParseError::None;
        ParseStatus status = parser.parseNext(event, error);

        ASSERT(status == ParseStatus::Error, "Expected error for cancel order_id 0");
        ASSERT(error == ParseError::InvalidOrderId, "Expected InvalidOrderId error");
    }

    // Modify with OrderId = 0
    {
        auto frame = wire::encode_modify_order(1, 0ULL, 100, 10);
        TcpParser parser;
        parser.append(frame);

        OrderEvent event;
        ParseError error = ParseError::None;
        ParseStatus status = parser.parseNext(event, error);

        ASSERT(status == ParseStatus::Error, "Expected error for modify order_id 0");
        ASSERT(error == ParseError::InvalidOrderId, "Expected InvalidOrderId error");
    }
}

// ─────────────────────────────────────────────
// 18. Truncated Frame / Client Disconnect
// ─────────────────────────────────────────────
TEST(test_truncated_frame_disconnect) {
    auto frame = wire::encode_limit_order(1, Side::Buy, 100, 10, TimeInForce::GTC);

    TcpParser parser;
    // Feed 7 bytes out of 17
    parser.append(frame.data(), 7);

    ASSERT(parser.hasPartialData(), "Parser should have partial data");
    ASSERT(parser.remainingBytes() == 7, "Expected 7 bytes");

    OrderEvent event;
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNext(event, error);
    ASSERT(status == ParseStatus::NeedMoreData, "Partial frame should need more data");

    // Client disconnect occurs -> reset parser state cleanly
    parser.reset();
    ASSERT(!parser.hasPartialData(), "Buffer should be empty after reset");
    ASSERT(parser.remainingBytes() == 0, "0 bytes remaining");

    // Assert parser is clean and ready for new data
    auto frame2 = wire::encode_market_order(2, Side::Sell, 5);
    parser.append(frame2);
    status = parser.parseNext(event, error);
    ASSERT(status == ParseStatus::Ok, "New connection should parse cleanly");
    ASSERT(event.type == EventType::MarketOrder && event.qty == 5, "Event must match");
}

// ─────────────────────────────────────────────
// 19. Ping / Pong Frame Parsing
// ─────────────────────────────────────────────
TEST(test_ping_pong_frame_parsing) {
    auto ping_frame = wire::encode_ping(0x123456789ABCDEF0ULL);
    ASSERT(ping_frame.size() == 11, "Expected 11-byte ping frame");

    TcpParser parser;
    parser.append(ping_frame);

    ParsedFrame frame{};
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNextFrame(frame, error);

    ASSERT(status == ParseStatus::Ok, "Expected Ok status");
    ASSERT(frame.category == FrameCategory::Session, "Expected Session category");
    ASSERT(frame.session.type == wire::MessageType::Ping, "Expected Ping msg type");
    ASSERT(frame.session.nonce == 0x123456789ABCDEF0ULL, "Expected nonce match");
}

// ─────────────────────────────────────────────
// 20. Query Stats Frame Parsing
// ─────────────────────────────────────────────
TEST(test_query_stats_frame_parsing) {
    auto stats_frame = wire::encode_query_stats();
    ASSERT(stats_frame.size() == 3, "Expected 3-byte query stats frame");

    TcpParser parser;
    parser.append(stats_frame);

    ParsedFrame frame{};
    ParseError error = ParseError::None;
    ParseStatus status = parser.parseNextFrame(frame, error);

    ASSERT(status == ParseStatus::Ok, "Expected Ok status");
    ASSERT(frame.category == FrameCategory::Query, "Expected Query category");
    ASSERT(frame.query.type == wire::MessageType::QueryStats, "Expected QueryStats msg type");
}

// ─────────────────────────────────────────────
// 21. Adversarial Fuzzed Wire Frames
// ─────────────────────────────────────────────
TEST(test_adversarial_fuzzed_wire_frames) {
    // 1. Frame with payload_len = 0 but msg_type = 0x01 (requires 24 bytes)
    {
        uint8_t raw[] = {0x00, 0x00, 0x01};
        TcpParser parser;
        parser.append(raw, sizeof(raw));
        ParsedFrame frame{};
        ParseError error = ParseError::None;
        ParseStatus st = parser.parseNextFrame(frame, error);
        ASSERT(st == ParseStatus::Error, "Must reject zero payload len for LimitOrder");
        ASSERT(error == ParseError::InvalidPayloadLength, "Expected InvalidPayloadLength");
    }

    // 2. Frame with payload_len = 129 (exceeding MAX_PAYLOAD_LENGTH 128)
    {
        uint8_t raw[] = {0x00, 0x81, 0x01};
        TcpParser parser;
        parser.append(raw, sizeof(raw));
        ParsedFrame frame{};
        ParseError error = ParseError::None;
        ParseStatus st = parser.parseNextFrame(frame, error);
        ASSERT(st == ParseStatus::Error, "Must reject payload > 128 bytes");
        ASSERT(error == ParseError::PayloadTooLarge, "Expected PayloadTooLarge");
    }

    // 3. Corrupt message type 0xFF
    {
        uint8_t raw[] = {0x00, 0x04, 0xFF, 0x01, 0x02, 0x03, 0x04};
        TcpParser parser;
        parser.append(raw, sizeof(raw));
        ParsedFrame frame{};
        ParseError error = ParseError::None;
        ParseStatus st = parser.parseNextFrame(frame, error);
        ASSERT(st == ParseStatus::Error, "Must reject unknown message type");
        ASSERT(error == ParseError::UnknownMessageType, "Expected UnknownMessageType");
    }
}

// ─────────────────────────────────────────────
// Main Test Runner
// ─────────────────────────────────────────────
int main() {
    std::cout << "\n===== Wire Protocol & TCP Parser Test Suite =====\n\n";

    RUN(test_limit_order_round_trip);
    RUN(test_market_order_round_trip);
    RUN(test_cancel_order_round_trip);
    RUN(test_modify_order_round_trip);
    RUN(test_partial_header);
    RUN(test_partial_payload);
    RUN(test_one_byte_at_a_time);
    RUN(test_multiple_frames_in_one_buffer);
    RUN(test_multiple_frames_with_final_partial_frame);
    RUN(test_wrong_payload_length);
    RUN(test_payload_too_large);
    RUN(test_unknown_message_type);
    RUN(test_invalid_side);
    RUN(test_invalid_tif);
    RUN(test_zero_quantity);
    RUN(test_invalid_price);
    RUN(test_zero_order_id);
    RUN(test_truncated_frame_disconnect);
    RUN(test_ping_pong_frame_parsing);
    RUN(test_query_stats_frame_parsing);
    RUN(test_adversarial_fuzzed_wire_frames);

    std::cout << "\n=================================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n\n";

    return (failed == 0) ? 0 : 1;
}
