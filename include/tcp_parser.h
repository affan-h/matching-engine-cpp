#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include "types.h"
#include "network_protocol.h"
#include "wire_protocol.h"

// ─────────────────────────────────────────────
// Parser Status & Errors
// ─────────────────────────────────────────────

enum class ParseStatus {
    Ok,           // A complete, valid OrderEvent was produced
    NeedMoreData, // Incomplete frame in buffer, waiting for more data
    Error         // Fatal protocol or validation error
};

enum class ParseError {
    None = 0,
    PayloadTooLarge,        // payload_len > MAX_PAYLOAD_LENGTH (64)
    InvalidPayloadLength,   // payload_len does not match expected size for msg_type
    UnknownMessageType,     // msg_type is not in [0x01..0x04]
    InvalidSide,            // side is not 0 (Buy) or 1 (Sell)
    InvalidTimeInForce,     // tif is not 0 (GTC), 1 (IOC), or 2 (FOK)
    InvalidPrice,           // price == 0 or price > MAX_PRICE_VAL (100000)
    InvalidQuantity,        // quantity == 0
    InvalidOrderId          // order_id == 0 for cancel/modify
};

inline const char* parseErrorToString(ParseError error) {
    switch (error) {
        case ParseError::None:                 return "None";
        case ParseError::PayloadTooLarge:      return "Payload too large (> 64 bytes)";
        case ParseError::InvalidPayloadLength: return "Invalid payload length for message type";
        case ParseError::UnknownMessageType:   return "Unknown message type";
        case ParseError::InvalidSide:          return "Invalid side (must be 0 for Buy or 1 for Sell)";
        case ParseError::InvalidTimeInForce:   return "Invalid time-in-force (must be 0 for GTC, 1 for IOC, 2 for FOK)";
        case ParseError::InvalidPrice:         return "Invalid price (must be 1..100000)";
        case ParseError::InvalidQuantity:      return "Invalid quantity (must be >= 1)";
        case ParseError::InvalidOrderId:       return "Invalid order ID (must be >= 1)";
        default:                               return "Unknown parse error";
    }
}

// ─────────────────────────────────────────────
// Incremental TCP Stream Parser
// ─────────────────────────────────────────────

class TcpParser {
private:
    std::vector<uint8_t> buffer;
    size_t read_offset = 0;

    void compact() {
        if (read_offset > 0) {
            if (read_offset == buffer.size()) {
                buffer.clear();
                read_offset = 0;
            } else if (read_offset >= 1024 && read_offset >= buffer.size() / 2) {
                buffer.erase(buffer.begin(), buffer.begin() + read_offset);
                read_offset = 0;
            }
        }
    }

public:
    TcpParser() {
        buffer.reserve(4096);
    }

    // Append incoming raw bytes from socket read
    void append(const uint8_t* data, size_t size) {
        if (size == 0) return;
        compact();
        buffer.insert(buffer.end(), data, data + size);
    }

    void append(const std::vector<uint8_t>& data) {
        append(data.data(), data.size());
    }

    // Try to parse the next frame from the accumulated buffer.
    // Returns:
    //   ParseStatus::Ok -> out_event is populated with validated OrderEvent.
    //   ParseStatus::NeedMoreData -> Buffer does not contain a complete frame yet.
    //   ParseStatus::Error -> Fatal error occurred; out_error has the exact error.
    ParseStatus parseNext(OrderEvent& out_event, ParseError& out_error) {
        out_error = ParseError::None;
        const size_t available = buffer.size() - read_offset;

        // Need at least 3 bytes for header
        if (available < wire::HEADER_SIZE) {
            return ParseStatus::NeedMoreData;
        }

        const uint8_t* header_ptr = &buffer[read_offset];
        const uint16_t payload_len = wire::read_u16_be(header_ptr);
        const uint8_t  msg_type_raw = header_ptr[2];

        // Validation 1: Maximum payload size
        if (payload_len > wire::MAX_PAYLOAD_LENGTH) {
            out_error = ParseError::PayloadTooLarge;
            return ParseStatus::Error;
        }

        // Validation 2: Unknown message type
        if (msg_type_raw < 0x01 || msg_type_raw > 0x04) {
            out_error = ParseError::UnknownMessageType;
            return ParseStatus::Error;
        }

        const wire::MessageType msg_type = static_cast<wire::MessageType>(msg_type_raw);

        // Validation 3: Exact payload length per message type
        size_t expected_len = 0;
        switch (msg_type) {
            case wire::MessageType::NewLimitOrder:
                expected_len = wire::LIMIT_ORDER_PAYLOAD_SIZE;
                break;
            case wire::MessageType::NewMarketOrder:
                expected_len = wire::MARKET_ORDER_PAYLOAD_SIZE;
                break;
            case wire::MessageType::CancelOrder:
                expected_len = wire::CANCEL_ORDER_PAYLOAD_SIZE;
                break;
            case wire::MessageType::ModifyOrder:
                expected_len = wire::MODIFY_ORDER_PAYLOAD_SIZE;
                break;
        }

        if (payload_len != expected_len) {
            out_error = ParseError::InvalidPayloadLength;
            return ParseStatus::Error;
        }

        // Check if full frame is available in buffer
        const size_t total_frame_len = wire::HEADER_SIZE + payload_len;
        if (available < total_frame_len) {
            return ParseStatus::NeedMoreData;
        }

        // Extract payload
        const uint8_t* payload = &buffer[read_offset + wire::HEADER_SIZE];

        switch (msg_type) {
            case wire::MessageType::NewLimitOrder: {
                uint32_t inst_id = wire::read_u32_be(payload + 0);
                uint8_t  side_raw = payload[4];
                uint32_t price_raw = wire::read_u32_be(payload + 5);
                uint32_t qty_raw = wire::read_u32_be(payload + 9);
                uint8_t  tif_raw = payload[13];

                if (side_raw > 1) {
                    out_error = ParseError::InvalidSide;
                    return ParseStatus::Error;
                }
                if (price_raw < wire::MIN_PRICE || price_raw > wire::MAX_PRICE_VAL) {
                    out_error = ParseError::InvalidPrice;
                    return ParseStatus::Error;
                }
                if (qty_raw < wire::MIN_QUANTITY) {
                    out_error = ParseError::InvalidQuantity;
                    return ParseStatus::Error;
                }
                if (tif_raw > 2) {
                    out_error = ParseError::InvalidTimeInForce;
                    return ParseStatus::Error;
                }

                out_event.type = EventType::LimitOrder;
                out_event.instrument = inst_id;
                out_event.id = 0;
                out_event.side = (side_raw == 0) ? Side::Buy : Side::Sell;
                out_event.price = static_cast<Price>(price_raw);
                out_event.qty = static_cast<Quantity>(qty_raw);
                out_event.tif = static_cast<TimeInForce>(tif_raw);
                break;
            }

            case wire::MessageType::NewMarketOrder: {
                uint32_t inst_id = wire::read_u32_be(payload + 0);
                uint8_t  side_raw = payload[4];
                uint32_t qty_raw = wire::read_u32_be(payload + 5);

                if (side_raw > 1) {
                    out_error = ParseError::InvalidSide;
                    return ParseStatus::Error;
                }
                if (qty_raw < wire::MIN_QUANTITY) {
                    out_error = ParseError::InvalidQuantity;
                    return ParseStatus::Error;
                }

                out_event.type = EventType::MarketOrder;
                out_event.instrument = inst_id;
                out_event.id = 0;
                out_event.side = (side_raw == 0) ? Side::Buy : Side::Sell;
                out_event.price = 0;
                out_event.qty = static_cast<Quantity>(qty_raw);
                out_event.tif = TimeInForce::IOC;
                break;
            }

            case wire::MessageType::CancelOrder: {
                uint32_t inst_id = wire::read_u32_be(payload + 0);
                uint64_t order_id = wire::read_u64_be(payload + 4);

                if (order_id < wire::MIN_ORDER_ID) {
                    out_error = ParseError::InvalidOrderId;
                    return ParseStatus::Error;
                }

                out_event.type = EventType::CancelOrder;
                out_event.instrument = inst_id;
                out_event.id = order_id;
                out_event.side = Side::Buy;
                out_event.price = 0;
                out_event.qty = 0;
                out_event.tif = TimeInForce::GTC;
                break;
            }

            case wire::MessageType::ModifyOrder: {
                uint32_t inst_id = wire::read_u32_be(payload + 0);
                uint64_t order_id = wire::read_u64_be(payload + 4);
                uint32_t new_price_raw = wire::read_u32_be(payload + 12);
                uint32_t new_qty_raw = wire::read_u32_be(payload + 16);

                if (order_id < wire::MIN_ORDER_ID) {
                    out_error = ParseError::InvalidOrderId;
                    return ParseStatus::Error;
                }
                if (new_price_raw < wire::MIN_PRICE || new_price_raw > wire::MAX_PRICE_VAL) {
                    out_error = ParseError::InvalidPrice;
                    return ParseStatus::Error;
                }
                if (new_qty_raw < wire::MIN_QUANTITY) {
                    out_error = ParseError::InvalidQuantity;
                    return ParseStatus::Error;
                }

                out_event.type = EventType::ModifyOrder;
                out_event.instrument = inst_id;
                out_event.id = order_id;
                out_event.side = Side::Buy;
                out_event.price = static_cast<Price>(new_price_raw);
                out_event.qty = static_cast<Quantity>(new_qty_raw);
                out_event.tif = TimeInForce::GTC;
                break;
            }
        }

        // Advance read offset past the successfully parsed frame
        read_offset += total_frame_len;
        compact();
        return ParseStatus::Ok;
    }

    // Convenience method to parse all available complete events in the buffer
    ParseStatus parseAll(std::vector<OrderEvent>& out_events, ParseError& out_error) {
        out_error = ParseError::None;
        while (true) {
            OrderEvent event;
            ParseStatus status = parseNext(event, out_error);
            if (status == ParseStatus::Ok) {
                out_events.push_back(event);
            } else if (status == ParseStatus::NeedMoreData) {
                return ParseStatus::Ok; // finished all complete frames
            } else {
                return ParseStatus::Error; // fatal parse error
            }
        }
    }

    // Returns whether there is unparsed data remaining in buffer
    bool hasPartialData() const {
        return (buffer.size() - read_offset) > 0;
    }

    // Returns number of unparsed bytes remaining in buffer
    size_t remainingBytes() const {
        return buffer.size() - read_offset;
    }

    // Reset buffer (e.g. on client disconnect or fatal error)
    void reset() {
        buffer.clear();
        read_offset = 0;
    }
};
