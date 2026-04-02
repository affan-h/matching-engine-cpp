#include <iostream>
#include "orderbook.h"

using namespace std;

OrderBook::OrderBook()
{
    bidLevels.resize(MAX_PRICE + 1, PriceLevel(0));
    askLevels.resize(MAX_PRICE + 1, PriceLevel(0));

    bestBid = -1;
    bestAsk = MAX_PRICE + 1;
}

bool OrderBook::hasBids() const
{
    return bestBid != -1;
}

bool OrderBook::hasAsks() const
{
    return bestAsk <= MAX_PRICE;
}

Price OrderBook::getBestBid() const
{
    return bestBid;
}

Price OrderBook::getBestAsk() const
{
    return bestAsk;
}

Order& OrderBook::getBestBidOrder()
{
    return bidLevels[bestBid].orders.front();
}

Order& OrderBook::getBestAskOrder()
{
    return askLevels[bestAsk].orders.front();
}

void OrderBook::insertBid(const Order& order)
{
    auto& level = bidLevels[order.price];

    if (level.price == 0)
        level.price = order.price;

    level.orders.push_back(order);
    auto it = prev(level.orders.end());

    level.totalVolume += order.quantity;
    orderLookup[order.id] = it;

    // mark active
    bidBitmap.set(order.price);

    if (order.price > bestBid)
        bestBid = order.price;
}

void OrderBook::insertAsk(const Order& order)
{
    auto& level = askLevels[order.price];

    if (level.price == 0)
        level.price = order.price;

    level.orders.push_back(order);
    auto it = prev(level.orders.end());

    level.totalVolume += order.quantity;
    orderLookup[order.id] = it;

    // mark active
    askBitmap.set(order.price);

    if (order.price < bestAsk)
        bestAsk = order.price;
}

void OrderBook::removeBestBid()
{
    auto& level = bidLevels[bestBid];

    auto it = level.orders.begin();

    level.totalVolume -= it->quantity;
    orderLookup.erase(it->id);
    level.orders.pop_front();

    if (level.orders.empty())
    {
        bidBitmap.reset(bestBid);

        // find next best bid
        int p = bestBid - 1;
        while (p >= 0 && !bidBitmap.test(p))
            --p;

        bestBid = p;
    }
}

void OrderBook::removeBestAsk()
{
    auto& level = askLevels[bestAsk];

    auto it = level.orders.begin();

    level.totalVolume -= it->quantity;
    orderLookup.erase(it->id);
    level.orders.pop_front();

    if (level.orders.empty())
    {
        askBitmap.reset(bestAsk);

        //find next best ask
        int p = bestAsk + 1;
        while (p <= MAX_PRICE && !askBitmap.test(p))
            ++p;

        bestAsk = p;
    }
}

bool OrderBook::cancelOrder(OrderId id)
{
    auto lookupIt = orderLookup.find(id);
    if (lookupIt == orderLookup.end())
        return false;

    auto orderIt = lookupIt->second;
    Order order = *orderIt;

    if (order.side == Side::Buy)
    {
        auto& level = bidLevels[order.price];

        level.totalVolume -= order.quantity;
        level.orders.erase(orderIt);

        if (level.orders.empty())
        {
            bidBitmap.reset(order.price);

            if (order.price == bestBid)
            {
                int p = bestBid - 1;
                while (p >= 0 && !bidBitmap.test(p))
                    --p;
                bestBid = p;
            }
        }
    }
    else
    {
        auto& level = askLevels[order.price];

        level.totalVolume -= order.quantity;
        level.orders.erase(orderIt);

        if (level.orders.empty())
        {
            askBitmap.reset(order.price);

            if (order.price == bestAsk)
            {
                int p = bestAsk + 1;
                while (p <= MAX_PRICE && !askBitmap.test(p))
                    ++p;
                bestAsk = p;
            }
        }
    }

    orderLookup.erase(lookupIt);
    return true;
}

bool OrderBook::getOrder(OrderId id, Order& outOrder)
{
    auto it = orderLookup.find(id);
    if (it == orderLookup.end())
        return false;

    outOrder = *(it->second);
    return true;
}

void OrderBook::printBook() const
{
    std::cout << "\n----- ORDER BOOK -----\n";

    std::cout << "\nASKS:\n";
    for (int p = bestAsk; p <= MAX_PRICE; ++p)
    {
        if (!askLevels[p].orders.empty())
        {
            std::cout << p << " : " << askLevels[p].totalVolume << "\n";
        }
    }

    std::cout << "\nBIDS:\n";
    for (int p = bestBid; p >= 0; --p)
    {
        if (!bidLevels[p].orders.empty())
        {
            std::cout << p << " : " << bidLevels[p].totalVolume << "\n";
        }
    }

    std::cout << "----------------------\n";
}