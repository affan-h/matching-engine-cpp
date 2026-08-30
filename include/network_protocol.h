#pragma once

#include <cstdint>
#include "types.h"

enum class EventType : uint8_t {
    LimitOrder,
    MarketOrder,
    CancelOrder,
    ModifyOrder
};

// Internal domain event submitted through the lock-free SPSC command queue
struct OrderEvent {
    EventType    type{EventType::LimitOrder};
    InstrumentId instrument{0};
    OrderId      id{0};              // Order ID (for cancel/modify)
    uint64_t     client_order_id{0}; // Client-provided correlation ID
    Side         side{Side::Buy};
    Price        price{0};
    Quantity     qty{0};
    TimeInForce  tif{TimeInForce::GTC};
    int          client_fd{-1};      // Originating client connection descriptor
};
