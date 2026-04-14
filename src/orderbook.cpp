#include <iostream>
#include "orderbook.h"

using namespace std;

OrderBook::OrderBook()
{
    bidLevels.resize(MAX_PRICE + 1, PriceLevel(0));
    askLevels.resize(MAX_PRICE + 1, PriceLevel(0));

    int numWords = (MAX_PRICE + WORD_SIZE) / WORD_SIZE;

    bidBitmap.resize(numWords, 0);
    askBitmap.resize(numWords, 0);

    orderLookup.resize(2'000'000, nullptr);
    bidLevels.reserve(MAX_PRICE + 1);
    askLevels.reserve(MAX_PRICE + 1);
}

inline void OrderBook::setBit(std::vector<uint64_t>& bitmap, int price)
{
    int word = price / 64;
    int bit  = price % 64;

    bitmap[word] |= (1ULL << bit);
}

inline void OrderBook::clearBit(std::vector<uint64_t>& bitmap, int price)
{
    int word = price / 64;
    int bit  = price % 64;

    bitmap[word] &= ~(1ULL << bit);
}

inline bool OrderBook::testBit(const std::vector<uint64_t>& bitmap, int price) const
{
    int word = price / 64;
    int bit  = price % 64;

    return (bitmap[word] & (1ULL << bit)) != 0;
}

inline int OrderBook::findNextAsk(int fromPrice) const
{
    int word = fromPrice / 64;
    int bit  = fromPrice % 64;

    // Mask current word (ignore lower bits)
    uint64_t w = askBitmap[word] & (~0ULL << bit);

    if (w != 0)
    {
        int b = __builtin_ctzll(w);
        return word * 64 + b;
    }

    // Check next words
    for (size_t i = word + 1; i < askBitmap.size(); ++i)
    {
        if (askBitmap[i] != 0)
        {
            int b = __builtin_ctzll(askBitmap[i]);
            return i * 64 + b;
        }
    }

    return MAX_PRICE + 1; // no ask
}

inline int OrderBook::findNextBid(int fromPrice) const
{
    int word = fromPrice / 64;
    int bit  = fromPrice % 64;

    // Mask current word (ignore higher bits)
    uint64_t w = bidBitmap[word] & ((1ULL << (bit + 1)) - 1);

    if (w != 0)
    {
        int b = 63 - __builtin_clzll(w);
        return word * 64 + b;
    }

    // Check previous words
    for (int i = word - 1; i >= 0; --i)
    {
        if (bidBitmap[i] != 0)
        {
            int b = 63 - __builtin_clzll(bidBitmap[i]);
            return i * 64 + b;
        }
    }

    return -1; // no bid
}

bool OrderBook::hasBids() const
{
    return findNextBid(MAX_PRICE) != -1;
}

bool OrderBook::hasAsks() const
{
    return findNextAsk(0) <= MAX_PRICE;
}

Price OrderBook::getBestBid() const
{
    return findNextBid(MAX_PRICE);
}

Price OrderBook::getBestAsk() const
{
    return findNextAsk(0);
}

Order& OrderBook::getBestBidOrder()
{
    int best = getBestBid();
    return *(bidLevels[best].head);
}

Order& OrderBook::getBestAskOrder()
{
    int best = getBestAsk();
    return *(askLevels[best].head);
}

void OrderBook::insertBid(const Order& order)
{
    auto& level = bidLevels[order.price];

    if (level.price == 0)
        level.price = order.price;

    Order* node = pool.allocate();
    //*node = order;
    node->id = order.id;
    node->side = order.side;
    node->price = order.price;
    node->quantity = order.quantity;
    node->timestamp = order.timestamp;

    node->prev = level.tail;
    node->next = nullptr;

    if (__builtin_expect(level.tail != nullptr, 1))
        level.tail->next = node;
    else
        level.head = node;

    level.tail = node;

    level.totalVolume += order.quantity;
    if (order.id >= orderLookup.size())
        orderLookup.resize(order.id + 1, nullptr);
    orderLookup[order.id] = node;

    setBit(bidBitmap, order.price);
}

void OrderBook::insertAsk(const Order& order)
{
    auto& level = askLevels[order.price];

    if (level.price == 0)
        level.price = order.price;

    Order* node = pool.allocate();
    //*node = order;
    node->id = order.id;
    node->side = order.side;
    node->price = order.price;
    node->quantity = order.quantity;
    node->timestamp = order.timestamp;

    node->prev = level.tail;
    node->next = nullptr;

    if (__builtin_expect(level.tail != nullptr, 1))
        level.tail->next = node;
    else
        level.head = node;

    level.tail = node;

    level.totalVolume += order.quantity;
    if (order.id >= orderLookup.size())
        orderLookup.resize(order.id + 1, nullptr);
    orderLookup[order.id] = node;

    setBit(askBitmap, order.price);
}

void OrderBook::removeBestBid()
{
    int best = getBestBid();
    auto& level = bidLevels[best];

    Order* node = level.head;

    level.totalVolume -= node->quantity;

    level.head = node->next;

    if (__builtin_expect(level.head != nullptr, 1))
        level.head->prev = nullptr;
    else
        level.tail = nullptr;

    orderLookup[node->id] = nullptr;

    pool.deallocate(node);

    if (__builtin_expect(level.head == nullptr, 0))
    {
        clearBit(bidBitmap, best);
    }
}

void OrderBook::removeBestAsk()
{
    int best = getBestAsk();
    auto& level = askLevels[best];

    Order* node = level.head;

    level.totalVolume -= node->quantity;

    level.head = node->next;

    if (__builtin_expect(level.head != nullptr, 1))
        level.head->prev = nullptr;
    else
        level.tail = nullptr;

    orderLookup[node->id] = nullptr;

    pool.deallocate(node);

    if (__builtin_expect(level.head == nullptr, 0))
    {
        clearBit(askBitmap, best);
    }
}

void OrderBook::reduceBidVolume(Price price, Quantity qty)
{
    bidLevels[price].totalVolume -= qty;
}

void OrderBook::reduceAskVolume(Price price, Quantity qty)
{
    askLevels[price].totalVolume -= qty;
}

bool OrderBook::cancelOrder(OrderId id)
{
    if (id >= orderLookup.size())
        return false;

    Order* node = orderLookup[id];
    if (!node)
        return false;
    const Order& order = *node;

    if (order.side == Side::Buy)
    {
        auto& level = bidLevels[order.price];

        level.totalVolume -= order.quantity;

        // unlink
        if (node->prev)
            node->prev->next = node->next;
        else
            level.head = node->next;

        if (node->next)
            node->next->prev = node->prev;
        else
            level.tail = node->prev;

        if (__builtin_expect(level.head == nullptr, 0))
            clearBit(bidBitmap, order.price);
    }
    else
    {
        auto& level = askLevels[order.price];

        level.totalVolume -= order.quantity;

        // unlink
        if (node->prev)
            node->prev->next = node->next;
        else
            level.head = node->next;

        if (node->next)
            node->next->prev = node->prev;
        else
            level.tail = node->prev;

        if (__builtin_expect(level.head == nullptr, 0))
            clearBit(askBitmap, order.price);
    }

    orderLookup[node->id] = nullptr;

    pool.deallocate(node);

    return true;
}

void OrderBook::printBook() const
{
    std::cout << "\n----- ORDER BOOK -----\n";

    std::cout << "\nASKS:\n";

    int p = findNextAsk(0);
    while (p <= MAX_PRICE)
    {
        if (askLevels[p].head != nullptr)
        {
            std::cout << p << " : " << askLevels[p].totalVolume << "\n";
        }

        p = findNextAsk(p + 1);
    }

    std::cout << "\nBIDS:\n";

    p = findNextBid(MAX_PRICE);
    while (p >= 0)
    {
        if (bidLevels[p].head != nullptr)
        {
            std::cout << p << " : " << bidLevels[p].totalVolume << "\n";
        }

        p = findNextBid(p - 1);
    }

    std::cout << "----------------------\n";
}

bool OrderBook::getOrder(OrderId id, Order& outOrder)
{
    if (id < 0 || id >= (OrderId)orderLookup.size())
        return false;

    if (orderLookup[id] == nullptr)
        return false;

    outOrder = *(orderLookup[id]);
    return true;
}