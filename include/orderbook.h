#pragma once

#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include "types.h"

using namespace std;

class OrderBook {
private:
    // Bids: highest price first
    std::map<int, std::list<Order>, std::greater<int>> bids;

    // Asks: lowest price first
    std::map<int, std::list<Order>> asks;

    // Order lookup: id -> (isBuy, iterator)
    std::unordered_map<int, std::pair<bool, std::list<Order>::iterator>> orderLookup;

    std::vector<ExecutionEvent> events;
    void match(Order& incoming, bool isMarket);

public:
    void addLimitOrder(int id, bool isBuy, int price, int quantity);
    void cancelOrder(int id);
    void printBook() const;
    
    void addMarketOrder(bool isBuy, int quantity);
    void modifyOrder(int id, int newPrice, int newQuantity);

    int getBestBid() const;
    int getBestAsk() const;

    const std::vector<ExecutionEvent>& getEvents() const;
    void printTrades() const;
    void clearEvents();
};