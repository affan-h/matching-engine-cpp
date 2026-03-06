#pragma once

#include <unordered_map>
#include <string>
#include "orderbook.h"

using namespace std;

class Exchange {
private:
    unordered_map<string, OrderBook> books;

public:
    void addLimitOrder(const string& symbol,
                       int id,
                       bool isBuy,
                       int price,
                       int quantity);

    void addMarketOrder(const string& symbol,
                        bool isBuy,
                        int quantity);

    void cancelOrder(const string& symbol, int id);
    void printBook(const string& symbol) const;

    void modifyOrder(const string& symbol,
                     int id,
                     int newPrice,
                     int newQuantity);
    

    int getBestBid(const string& symbol) const;
    int getBestAsk(const string& symbol) const;

    const vector<ExecutionEvent>& getEvents(const string& symbol) const;
    void printTrades(const string& symbol) const;
    void clearEvents(const string& symbol);
};