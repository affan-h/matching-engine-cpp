#pragma once
#include "types.h"

enum class EventType : uint8_t {
    LimitOrder,
    MarketOrder,
    CancelOrder,
    ModifyOrder
};

// This struct simulates the binary payload we receive from a network socket
struct OrderEvent {
    EventType type;
    InstrumentId instrument;
    OrderId id; // Only used for cancel/modify
    Side side;
    Price price;
    Quantity qty;
    TimeInForce tif;
};