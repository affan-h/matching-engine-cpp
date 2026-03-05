#include <iostream>
#include "orderbook.h"

using namespace std;

void OrderBook::match(Order& incoming, bool isMarket) {

    if (incoming.isBuy) {

        while (!asks.empty() && incoming.quantity > 0) {

            auto bestAskIt = asks.begin();
            int bestAskPrice = bestAskIt->first;

            // For limit orders, enforce price condition
            if (!isMarket && bestAskPrice > incoming.price)
                break;

            auto& orderList = bestAskIt->second;

            while (!orderList.empty() && incoming.quantity > 0) {

                Order& resting = orderList.front();
                int tradedQty = min(incoming.quantity, resting.quantity);

                events.emplace_back(EventType::TRADE, bestAskPrice, tradedQty);

                incoming.quantity -= tradedQty;
                resting.quantity -= tradedQty;

                if (resting.quantity == 0) {
                    orderLookup.erase(resting.id);
                    orderList.pop_front();
                }
            }

            if (orderList.empty()) {
                asks.erase(bestAskIt);
            }
        }

    } else {  // SELL SIDE

        while (!bids.empty() && incoming.quantity > 0) {

            auto bestBidIt = bids.begin();
            int bestBidPrice = bestBidIt->first;

            if (!isMarket && bestBidPrice < incoming.price)
                break;

            auto& orderList = bestBidIt->second;

            while (!orderList.empty() && incoming.quantity > 0) {

                Order& resting = orderList.front();
                int tradedQty = min(incoming.quantity, resting.quantity);

                events.emplace_back(EventType::TRADE, bestBidPrice, tradedQty);

                incoming.quantity -= tradedQty;
                resting.quantity -= tradedQty;

                if (resting.quantity == 0) {
                    orderLookup.erase(resting.id);
                    orderList.pop_front();
                }
            }

            if (orderList.empty()) {
                bids.erase(bestBidIt);
            }
        }
    }
}

void OrderBook::addLimitOrder(int id, bool isBuy, int price, int quantity) {

    if (orderLookup.count(id)) {
        cout << "Order ID already exists.\n";
        return;
    }

    Order incoming(id, isBuy, price, quantity);

    // Try matching first
    match(incoming, false);

    // If still has quantity, insert into book
    if (incoming.quantity > 0) {

        if (isBuy) {
            bids[price].push_back(incoming);
            auto it = prev(bids[price].end());
            orderLookup[id] = {true, it};
        } else {
            asks[price].push_back(incoming);
            auto it = prev(asks[price].end());
            orderLookup[id] = {false, it};
        }
    }
}

void OrderBook::cancelOrder(int id) {
    if (!orderLookup.count(id)) {
        cout << "Order not found.\n";
        return;
    }

    auto [isBuy, it] = orderLookup[id];

    if (isBuy) {
        int price = it->price;
        bids[price].erase(it);

        if (bids[price].empty()) {
            bids.erase(price);
        }
    } else {
        int price = it->price;
        asks[price].erase(it);

        if (asks[price].empty()) {
            asks.erase(price);
        }
    }

    orderLookup.erase(id);
}

void OrderBook::printBook() const {
    cout << "\n--- ORDER BOOK ---\n";

    cout << "ASKS:\n";
    for (const auto& [price, orders] : asks) {
        int totalQty = 0;
        for (const auto& order : orders)
            totalQty += order.quantity;

        cout << price << " : " << totalQty << "\n";
    }

    cout << "BIDS:\n";
    for (const auto& [price, orders] : bids) {
        int totalQty = 0;
        for (const auto& order : orders)
            totalQty += order.quantity;

        cout << price << " : " << totalQty << "\n";
    }
}

void OrderBook::addMarketOrder(bool isBuy, int quantity) {

    Order incoming(-1, isBuy, 0, quantity);

    match(incoming, true);

    if (incoming.quantity > 0) {
        events.emplace_back(EventType::UNFILLED_MARKET, 0, incoming.quantity);
    }
}

void OrderBook::modifyOrder(int id, int newPrice, int newQuantity) {

    if (!orderLookup.count(id)) {
        cout << "Order not found.\n";
        return;
    }

    bool isBuy = orderLookup[id].first;

    cancelOrder(id);

    addLimitOrder(id, isBuy, newPrice, newQuantity);
}

int OrderBook::getBestBid() const {
    if (bids.empty())
        return -1;  // no bid
    return bids.begin()->first;
}

int OrderBook::getBestAsk() const {
    if (asks.empty())
        return -1;  // no ask
    return asks.begin()->first;
}

const vector<ExecutionEvent>& OrderBook::getEvents() const {
    return events;
}

void OrderBook::printTrades() const {

    cout << "\nExecution Events:\n";

    for (const auto& e : events) {

        if (e.type == EventType::TRADE)
            cout << "TRADE: " << e.quantity << " @ " << e.price << "\n";

        else if (e.type == EventType::UNFILLED_MARKET)
            cout << "UNFILLED MARKET QTY: " << e.quantity << "\n";
    }
}

void OrderBook::clearEvents() {
    events.clear();
}