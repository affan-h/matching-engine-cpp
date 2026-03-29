#include <iostream>
#include "orderbook.h"

using namespace std;

bool OrderBook::hasBids() const
{
    return !bids.empty();
}

bool OrderBook::hasAsks() const
{
    return !asks.empty();
}

Price OrderBook::getBestBid() const
{
    return bids.begin()->first;
}

Price OrderBook::getBestAsk() const
{
    return asks.begin()->first;
}

Order& OrderBook::getBestBidOrder()
{
    return bids.begin()->second.orders.front();
}

Order& OrderBook::getBestAskOrder()
{
    return asks.begin()->second.orders.front();
}

void OrderBook::insertBid(const Order& order)
{
    auto it = bids.find(order.price);

    if (it == bids.end())
    {
        it = bids.emplace(order.price, PriceLevel(order.price)).first;
    }

    it->second.orders.push_back(order);

    auto orderIt = std::prev(it->second.orders.end());

    it->second.totalVolume += order.quantity;

    orderLookup[order.id] = orderIt;
}

void OrderBook::insertAsk(const Order& order)
{
    auto it = asks.find(order.price);

    if (it == asks.end())
    {
        it = asks.emplace(order.price, PriceLevel(order.price)).first;
    }

    it->second.orders.push_back(order);

    auto orderIt = std::prev(it->second.orders.end());

    it->second.totalVolume += order.quantity;

    orderLookup[order.id] = orderIt;
}

void OrderBook::removeBestBid()
{
    auto levelIt = bids.begin();

    auto &level = levelIt->second;

    auto orderIt = level.orders.begin();

    level.totalVolume -= orderIt->quantity;

    orderLookup.erase(orderIt->id);

    level.orders.pop_front();

    if (level.orders.empty())
    {
        bids.erase(levelIt);
    }
}

void OrderBook::removeBestAsk()
{
    auto levelIt = asks.begin();

    auto &level = levelIt->second;

    auto orderIt = level.orders.begin();

    level.totalVolume -= orderIt->quantity;

    orderLookup.erase(orderIt->id);

    level.orders.pop_front();

    if (level.orders.empty())
    {
        asks.erase(levelIt);
    }
}

bool OrderBook::cancelOrder(OrderId id)
{
    auto lookupIt = orderLookup.find(id);

    if (lookupIt == orderLookup.end())
        return false;

    auto orderIt = lookupIt->second;

    const Order &order = *orderIt;

    if (order.side == Side::Buy)
    {
        auto levelIt = bids.find(order.price);

        auto &level = levelIt->second;

        level.totalVolume -= order.quantity;

        level.orders.erase(orderIt);

        if (level.orders.empty())
            bids.erase(levelIt);
    }
    else
    {
        auto levelIt = asks.find(order.price);

        auto &level = levelIt->second;

        level.totalVolume -= order.quantity;

        level.orders.erase(orderIt);

        if (level.orders.empty())
            asks.erase(levelIt);
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
    for (const auto& [price, level] : asks)
    {
        std::cout << price << " : " << level.totalVolume << "\n";
    }

    std::cout << "\nBIDS:\n";
    for (const auto& [price, level] : bids)
    {
        std::cout << price << " : " << level.totalVolume << "\n";
    }

    std::cout << "----------------------\n";
}