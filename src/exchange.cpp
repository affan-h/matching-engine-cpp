#include "exchange.h"
#include <string>
#include <vector>
using namespace std;

void Exchange::addLimitOrder(const string& symbol, int id, bool isBuy, int price, int quantity) {
    books[symbol].addLimitOrder(id, isBuy, price, quantity);
}

void Exchange::addMarketOrder(const string& symbol, bool isBuy, int quantity) {
    return books[symbol].addMarketOrder(isBuy, quantity);
}

void Exchange::cancelOrder(const string& symbol, int id) {
    if (books.count(symbol))
        books[symbol].cancelOrder(id);
}

void Exchange::modifyOrder(const string& symbol, int id, int newPrice, int newQuantity) {
    if (books.count(symbol))
        books[symbol].modifyOrder(id, newPrice, newQuantity);
}

void Exchange::printBook(const string& symbol) const {
    if (books.count(symbol))
        books.at(symbol).printBook();
}

int Exchange::getBestBid(const string& symbol) const {
    if (!books.count(symbol))
        return -1;
    return books.at(symbol).getBestBid();
}

int Exchange::getBestAsk(const string& symbol) const {
    if (!books.count(symbol))
        return -1;
    return books.at(symbol).getBestAsk();
}

const vector<ExecutionEvent>& Exchange::getEvents(const string& symbol) const {
    return books.at(symbol).getEvents();
}

void Exchange::printTrades(const string& symbol) const {
    books.at(symbol).printTrades();
}

void Exchange::clearEvents(const string& symbol) {
    books[symbol].clearEvents();
}