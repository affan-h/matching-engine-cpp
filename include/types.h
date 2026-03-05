#pragma once
#include <list>
#include <vector>

enum class EventType {
    TRADE,
    UNFILLED_MARKET
};

struct ExecutionEvent {
    EventType type;
    int price;
    int quantity;

    ExecutionEvent(EventType t, int p, int q)
        : type(t), price(p), quantity(q) {}
};

struct Order {
    int id;
    bool isBuy;     // true = buy, false = sell
    int price;
    int quantity;

    Order(int id_, bool isBuy_, int price_, int quantity_)
        : id(id_), isBuy(isBuy_), price(price_), quantity(quantity_) {}
};