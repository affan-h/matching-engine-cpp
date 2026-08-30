#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <algorithm>
#include "types.h"
#include "network_protocol.h"
#include "outbound_events.h"
#include "read_model.h"

// ─────────────────────────────────────────────
// Wire Protocol Constants
// ─────────────────────────────────────────────

namespace wire {

// Header format: [2 bytes: payload_len (BE)] [1 byte: msg_type]
constexpr size_t HEADER_SIZE = 3;
constexpr size_t MAX_PAYLOAD_LENGTH = 64;
constexpr size_t MAX_QUERY_RESPONSE_PAYLOAD_LENGTH = 4096;

// Exact expected payload sizes for ingress message types
constexpr size_t LIMIT_ORDER_PAYLOAD_SIZE  = 14; // 4 (inst) + 1 (side) + 4 (px) + 4 (qty) + 1 (tif)
constexpr size_t MARKET_ORDER_PAYLOAD_SIZE = 9;  // 4 (inst) + 1 (side) + 4 (qty)
constexpr size_t CANCEL_ORDER_PAYLOAD_SIZE = 12; // 4 (inst) + 8 (order_id)
constexpr size_t MODIFY_ORDER_PAYLOAD_SIZE = 20; // 4 (inst) + 8 (order_id) + 4 (new_px) + 4 (new_qty)

constexpr size_t QUERY_BOOK_PAYLOAD_SIZE   = 4;  // 4 (inst)
constexpr size_t QUERY_TRADES_PAYLOAD_SIZE = 8;  // 4 (inst) + 4 (limit)
constexpr size_t QUERY_ORDER_PAYLOAD_SIZE  = 8;  // 8 (order_id)
constexpr size_t QUERY_ORDER_RESP_PAYLOAD_SIZE = 39;

// Value constraints
constexpr Price    MIN_PRICE     = 1;
constexpr Price    MAX_PRICE_VAL = 100000;
constexpr Quantity MIN_QUANTITY  = 1;
constexpr OrderId  MIN_ORDER_ID  = 1;

// Message Types
enum class MessageType : uint8_t {
    // Commands (0x01 - 0x0F)
    NewLimitOrder       = 0x01,
    NewMarketOrder      = 0x02,
    CancelOrder         = 0x03,
    ModifyOrder         = 0x04,

    // Queries (0x10 - 0x1F)
    QueryBook           = 0x10,
    QueryTrades         = 0x11,
    QueryOrder          = 0x12,

    // Query Responses (0x80 - 0x8F)
    QueryBookResponse   = 0x80,
    QueryTradesResponse = 0x81,
    QueryOrderResponse  = 0x82,
    QueryErrorResponse  = 0x8F
};

// Wire Side Codes
enum class WireSide : uint8_t {
    Buy  = 0x00,
    Sell = 0x01
};

// Wire TimeInForce Codes
enum class WireTimeInForce : uint8_t {
    GTC = 0x00,
    IOC = 0x01,
    FOK = 0x02
};

// ─────────────────────────────────────────────
// Endian-Safe Byte Readers (Big-Endian / Network Order)
// ─────────────────────────────────────────────

inline uint16_t read_u16_be(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) |
                                 static_cast<uint16_t>(p[1]));
}

inline uint32_t read_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           (static_cast<uint32_t>(p[3]));
}

inline uint64_t read_u64_be(const uint8_t* p) {
    return (static_cast<uint64_t>(p[0]) << 56) |
           (static_cast<uint64_t>(p[1]) << 48) |
           (static_cast<uint64_t>(p[2]) << 40) |
           (static_cast<uint64_t>(p[3]) << 32) |
           (static_cast<uint64_t>(p[4]) << 24) |
           (static_cast<uint64_t>(p[5]) << 16) |
           (static_cast<uint64_t>(p[6]) << 8)  |
           (static_cast<uint64_t>(p[7]));
}

// ─────────────────────────────────────────────
// Endian-Safe Byte Writers (Big-Endian / Network Order)
// ─────────────────────────────────────────────

inline void write_u16_be(uint8_t* p, uint16_t val) {
    p[0] = static_cast<uint8_t>((val >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(val & 0xFF);
}

inline void write_u32_be(uint8_t* p, uint32_t val) {
    p[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(val & 0xFF);
}

inline void write_u64_be(uint8_t* p, uint64_t val) {
    p[0] = static_cast<uint8_t>((val >> 56) & 0xFF);
    p[1] = static_cast<uint8_t>((val >> 48) & 0xFF);
    p[2] = static_cast<uint8_t>((val >> 40) & 0xFF);
    p[3] = static_cast<uint8_t>((val >> 32) & 0xFF);
    p[4] = static_cast<uint8_t>((val >> 24) & 0xFF);
    p[5] = static_cast<uint8_t>((val >> 16) & 0xFF);
    p[6] = static_cast<uint8_t>((val >> 8) & 0xFF);
    p[7] = static_cast<uint8_t>(val & 0xFF);
}

// ─────────────────────────────────────────────
// Command Frame Encoders
// ─────────────────────────────────────────────

inline std::vector<uint8_t> encode_limit_order(
    InstrumentId instrument_id,
    Side side,
    Price price,
    Quantity qty,
    TimeInForce tif)
{
    std::vector<uint8_t> frame(HEADER_SIZE + LIMIT_ORDER_PAYLOAD_SIZE);
    write_u16_be(&frame[0], static_cast<uint16_t>(LIMIT_ORDER_PAYLOAD_SIZE));
    frame[2] = static_cast<uint8_t>(MessageType::NewLimitOrder);
    write_u32_be(&frame[3], instrument_id);
    frame[7] = (side == Side::Buy) ? static_cast<uint8_t>(WireSide::Buy)
                                   : static_cast<uint8_t>(WireSide::Sell);
    write_u32_be(&frame[8], static_cast<uint32_t>(price));
    write_u32_be(&frame[12], static_cast<uint32_t>(qty));
    frame[16] = static_cast<uint8_t>(tif);
    return frame;
}

inline std::vector<uint8_t> encode_market_order(
    InstrumentId instrument_id,
    Side side,
    Quantity qty)
{
    std::vector<uint8_t> frame(HEADER_SIZE + MARKET_ORDER_PAYLOAD_SIZE);
    write_u16_be(&frame[0], static_cast<uint16_t>(MARKET_ORDER_PAYLOAD_SIZE));
    frame[2] = static_cast<uint8_t>(MessageType::NewMarketOrder);
    write_u32_be(&frame[3], instrument_id);
    frame[7] = (side == Side::Buy) ? static_cast<uint8_t>(WireSide::Buy)
                                   : static_cast<uint8_t>(WireSide::Sell);
    write_u32_be(&frame[8], static_cast<uint32_t>(qty));
    return frame;
}

inline std::vector<uint8_t> encode_cancel_order(
    InstrumentId instrument_id,
    OrderId order_id)
{
    std::vector<uint8_t> frame(HEADER_SIZE + CANCEL_ORDER_PAYLOAD_SIZE);
    write_u16_be(&frame[0], static_cast<uint16_t>(CANCEL_ORDER_PAYLOAD_SIZE));
    frame[2] = static_cast<uint8_t>(MessageType::CancelOrder);
    write_u32_be(&frame[3], instrument_id);
    write_u64_be(&frame[7], order_id);
    return frame;
}

inline std::vector<uint8_t> encode_modify_order(
    InstrumentId instrument_id,
    OrderId order_id,
    Price new_price,
    Quantity new_qty)
{
    std::vector<uint8_t> frame(HEADER_SIZE + MODIFY_ORDER_PAYLOAD_SIZE);
    write_u16_be(&frame[0], static_cast<uint16_t>(MODIFY_ORDER_PAYLOAD_SIZE));
    frame[2] = static_cast<uint8_t>(MessageType::ModifyOrder);
    write_u32_be(&frame[3], instrument_id);
    write_u64_be(&frame[7], order_id);
    write_u32_be(&frame[15], static_cast<uint32_t>(new_price));
    write_u32_be(&frame[19], static_cast<uint32_t>(new_qty));
    return frame;
}

// ─────────────────────────────────────────────
// Query Frame Encoders
// ─────────────────────────────────────────────

inline std::vector<uint8_t> encode_query_book(InstrumentId instrument_id) {
    std::vector<uint8_t> frame(HEADER_SIZE + QUERY_BOOK_PAYLOAD_SIZE);
    write_u16_be(&frame[0], static_cast<uint16_t>(QUERY_BOOK_PAYLOAD_SIZE));
    frame[2] = static_cast<uint8_t>(MessageType::QueryBook);
    write_u32_be(&frame[3], instrument_id);
    return frame;
}

inline std::vector<uint8_t> encode_query_trades(InstrumentId instrument_id, uint32_t limit) {
    std::vector<uint8_t> frame(HEADER_SIZE + QUERY_TRADES_PAYLOAD_SIZE);
    write_u16_be(&frame[0], static_cast<uint16_t>(QUERY_TRADES_PAYLOAD_SIZE));
    frame[2] = static_cast<uint8_t>(MessageType::QueryTrades);
    write_u32_be(&frame[3], instrument_id);
    write_u32_be(&frame[7], limit);
    return frame;
}

inline std::vector<uint8_t> encode_query_order(OrderId order_id) {
    std::vector<uint8_t> frame(HEADER_SIZE + QUERY_ORDER_PAYLOAD_SIZE);
    write_u16_be(&frame[0], static_cast<uint16_t>(QUERY_ORDER_PAYLOAD_SIZE));
    frame[2] = static_cast<uint8_t>(MessageType::QueryOrder);
    write_u64_be(&frame[3], order_id);
    return frame;
}

// ─────────────────────────────────────────────
// Query Response Encoders
// ─────────────────────────────────────────────

inline std::vector<uint8_t> encode_query_book_response(const L2BookState& book) {
    uint8_t b_count = std::min(book.bid_count, static_cast<uint8_t>(events::MAX_L2_DEPTH));
    uint8_t a_count = std::min(book.ask_count, static_cast<uint8_t>(events::MAX_L2_DEPTH));
    size_t payload_len = 4 + 8 + 8 + 1 + 1 + (b_count + a_count) * 8;

    std::vector<uint8_t> frame(HEADER_SIZE + payload_len);
    write_u16_be(&frame[0], static_cast<uint16_t>(payload_len));
    frame[2] = static_cast<uint8_t>(MessageType::QueryBookResponse);

    uint8_t* p = &frame[3];
    write_u32_be(p, book.instrument_id); p += 4;
    write_u64_be(p, book.sequence);      p += 8;
    write_u64_be(p, static_cast<uint64_t>(book.timestamp)); p += 8;
    *p++ = b_count;
    *p++ = a_count;

    for (size_t i = 0; i < b_count; ++i) {
        write_u32_be(p, static_cast<uint32_t>(book.bids[i].price));    p += 4;
        write_u32_be(p, static_cast<uint32_t>(book.bids[i].quantity)); p += 4;
    }
    for (size_t i = 0; i < a_count; ++i) {
        write_u32_be(p, static_cast<uint32_t>(book.asks[i].price));    p += 4;
        write_u32_be(p, static_cast<uint32_t>(book.asks[i].quantity)); p += 4;
    }

    return frame;
}

inline std::vector<uint8_t> encode_query_trades_response(InstrumentId inst_id, const std::vector<TradeRecord>& trades) {
    uint16_t count = static_cast<uint16_t>(std::min(trades.size(), static_cast<size_t>(100)));
    size_t payload_len = 4 + 2 + count * 41;

    std::vector<uint8_t> frame(HEADER_SIZE + payload_len);
    write_u16_be(&frame[0], static_cast<uint16_t>(payload_len));
    frame[2] = static_cast<uint8_t>(MessageType::QueryTradesResponse);

    uint8_t* p = &frame[3];
    write_u32_be(p, inst_id); p += 4;
    write_u16_be(p, count);   p += 2;

    for (size_t i = 0; i < count; ++i) {
        const auto& t = trades[i];
        write_u64_be(p, t.trade_id);     p += 8;
        write_u64_be(p, t.buy_order_id); p += 8;
        write_u64_be(p, t.sell_order_id);p += 8;
        write_u32_be(p, static_cast<uint32_t>(t.price));    p += 4;
        write_u32_be(p, static_cast<uint32_t>(t.quantity)); p += 4;
        *p++ = (t.aggressor_side == Side::Buy) ? 0x00 : 0x01;
        write_u64_be(p, static_cast<uint64_t>(t.timestamp)); p += 8;
    }

    return frame;
}

inline std::vector<uint8_t> encode_query_order_response(bool found, const OrderRecord& order) {
    size_t payload_len = QUERY_ORDER_RESP_PAYLOAD_SIZE;

    std::vector<uint8_t> frame(HEADER_SIZE + payload_len);
    write_u16_be(&frame[0], static_cast<uint16_t>(payload_len));
    frame[2] = static_cast<uint8_t>(MessageType::QueryOrderResponse);

    uint8_t* p = &frame[3];
    *p++ = found ? 1 : 0;
    write_u64_be(p, order.order_id);      p += 8;
    write_u32_be(p, order.instrument_id); p += 4;
    *p++ = (order.side == Side::Buy) ? 0x00 : 0x01;
    write_u32_be(p, static_cast<uint32_t>(order.price));        p += 4;
    write_u32_be(p, static_cast<uint32_t>(order.original_qty)); p += 4;
    write_u32_be(p, static_cast<uint32_t>(order.remaining_qty));p += 4;
    write_u32_be(p, static_cast<uint32_t>(order.filled_qty));   p += 4;
    *p++ = static_cast<uint8_t>(order.status);
    write_u64_be(p, static_cast<uint64_t>(order.timestamp));    p += 8;

    return frame;
}

} // namespace wire
