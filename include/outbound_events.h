#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "types.h"

namespace events {

// Outbound event categories emitted by MatchingEngine to the Read Model Projector
enum class OutboundEventType : uint8_t {
    Trade       = 0x01,
    L2Update    = 0x02,
    OrderState  = 0x03
};

// Lifecycle state of an order
enum class OrderStatus : uint8_t {
    New             = 0x01, // Placed and resting in book
    PartiallyFilled = 0x02, // Partially matched, remainder resting or cancelled
    Filled          = 0x03, // Completely filled
    Cancelled       = 0x04, // Cancelled by user or IOC/FOK residual
    Rejected        = 0x05  // Rejected (e.g. FOK liquidity failure or invalid params)
};

// Machine-readable rejection and error codes
enum class RejectCode : uint8_t {
    None                     = 0x00,
    UnknownInstrument        = 0x01,
    InvalidPriceQty          = 0x02,
    InsufficientLiquidityFOK = 0x03,
    OrderNotFound            = 0x04,
    QueueFull                = 0x05
};

constexpr size_t MAX_L2_DEPTH = 10;

// Fixed-size price level representation for zero-allocation snapshots
struct PriceLevelRecord {
    Price price{0};
    Quantity quantity{0};
};

// Execution trade event
struct TradeEventPayload {
    uint64_t trade_id{0};
    InstrumentId instrument_id{0};
    OrderId buy_order_id{0};
    OrderId sell_order_id{0};
    Side aggressor_side{Side::Buy};
    Price price{0};
    Quantity quantity{0};
    Timestamp timestamp{0};
    uint64_t sequence{0};
};

// Fixed-size L2 order book depth snapshot update
struct L2UpdateEventPayload {
    InstrumentId instrument_id{0};
    uint8_t bid_count{0};
    uint8_t ask_count{0};
    PriceLevelRecord bids[MAX_L2_DEPTH]{};
    PriceLevelRecord asks[MAX_L2_DEPTH]{};
    Timestamp timestamp{0};
    uint64_t sequence{0};
};

// Order lifecycle status event with client correlation and rejection reason
struct OrderStateEventPayload {
    OrderId order_id{0};
    uint64_t client_order_id{0};
    InstrumentId instrument_id{0};
    Side side{Side::Buy};
    Price price{0};
    Quantity original_qty{0};
    Quantity remaining_qty{0};
    Quantity filled_qty{0};
    OrderStatus status{OrderStatus::New};
    RejectCode reject_code{RejectCode::None};
    Timestamp timestamp{0};
    uint64_t sequence{0};
};

// Fixed-size tagged union for lock-free SPSC transport (zero heap allocations)
struct OutboundEvent {
    OutboundEventType type{OutboundEventType::Trade};
    union {
        TradeEventPayload trade;
        L2UpdateEventPayload l2;
        OrderStateEventPayload order;
    };

    OutboundEvent() : type(OutboundEventType::Trade), trade{} {}
};

} // namespace events
