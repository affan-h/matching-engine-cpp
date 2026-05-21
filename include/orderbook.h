#pragma once

#include <map>
#include <list>
#include <vector>
#include <cstdint>
#include "types.h"
#include "price_level.h"
#include "order_pool.h"

class OrderBook {
private:
    static const int MAX_PRICE = 100000;
    static const int WORD_SIZE = 64;

    std::vector<PriceLevel> bidLevels;
    std::vector<PriceLevel> askLevels;

    std::vector<uint64_t> bidBitmap;
    std::vector<uint64_t> askBitmap;

    std::vector<Order*> orderLookup;    
    inline void setBit(std::vector<uint64_t>& bitmap, int price);
    inline void clearBit(std::vector<uint64_t>& bitmap, int price);
    inline bool testBit(const std::vector<uint64_t>& bitmap, int price) const;

    inline int findNextAsk(int fromPrice) const;
    inline int findNextBid(int fromPrice) const;

    OrderPool pool;

public:
    OrderBook();

    // Book state
    bool hasBids() const;
    bool hasAsks() const;

    Price getBestBid() const;
    Price getBestAsk() const;

    Order& getBestBidOrder();
    Order& getBestAskOrder();

    // Insert operations
    void insertBid(const Order& order);
    void insertAsk(const Order& order);

    // Remove best order (after fill)
    void removeBestBid();
    void removeBestAsk();

    // Order management
    bool cancelOrder(OrderId orderId);
    bool getOrder(OrderId id, Order& outOrder);

    bool reduceOrderSize(OrderId id, Quantity newQty);

    void reduceBidVolume(Price price, Quantity qty);
    void reduceAskVolume(Price price, Quantity qty);

    // Returns total fillable volume at or better than price, for FOK check
    Quantity getAvailableVolume(Side side, Price limitPrice) const;

    void printBook() const;
};