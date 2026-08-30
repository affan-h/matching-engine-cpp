#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include "types.h"
#include "network_protocol.h"

// ─────────────────────────────────────────────
// Wire Protocol Constants
// ─────────────────────────────────────────────

namespace wire {

// Header format: [2 bytes: payload_len (BE)] [1 byte: msg_type]
constexpr size_t HEADER_SIZE = 3;
constexpr size_t MAX_PAYLOAD_LENGTH = 64;

// Exact expected payload sizes for each message type
constexpr size_t LIMIT_ORDER_PAYLOAD_SIZE  = 14; // 4 (inst) + 1 (side) + 4 (px) + 4 (qty) + 1 (tif)
constexpr size_t MARKET_ORDER_PAYLOAD_SIZE = 9;  // 4 (inst) + 1 (side) + 4 (qty)
constexpr size_t CANCEL_ORDER_PAYLOAD_SIZE = 12; // 4 (inst) + 8 (order_id)
constexpr size_t MODIFY_ORDER_PAYLOAD_SIZE = 20; // 4 (inst) + 8 (order_id) + 4 (new_px) + 4 (new_qty)

// Value constraints
constexpr Price    MIN_PRICE     = 1;
constexpr Price    MAX_PRICE_VAL = 100000;
constexpr Quantity MIN_QUANTITY  = 1;
constexpr OrderId  MIN_ORDER_ID  = 1;

// Message Types
enum class MessageType : uint8_t {
    NewLimitOrder  = 0x01,
    NewMarketOrder = 0x02,
    CancelOrder    = 0x03,
    ModifyOrder    = 0x04
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
// Frame Encoders (for clients and testing)
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

} // namespace wire
