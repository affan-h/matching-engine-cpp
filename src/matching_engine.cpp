#include "matching_engine.h"
#include <iostream>
using namespace std;

Trade MatchingEngine::createTrade(
    const string& symbol,
    const Order& incoming,
    const Order& resting,
    Quantity qty)
{
    Trade trade;

    trade.symbol = symbol;
    trade.price = resting.price;
    trade.quantity = qty;
    trade.timestamp = ++currentTimestamp;
    trade.tradeId = ++nextTradeId;

    if (incoming.side == Side::Buy)
    {
        trade.buyOrderId = incoming.id;
        trade.sellOrderId = resting.id;
    }
    else
    {
        trade.buyOrderId = resting.id;
        trade.sellOrderId = incoming.id;
    }

    return trade;
}

void MatchingEngine::reportTrade(const Trade& trade)
{
    std::cout << "TRADE "
              << trade.symbol
              << " TID=" << trade.tradeId
              << " TIME=" << trade.timestamp
              << " BUY=" << trade.buyOrderId
              << " SELL=" << trade.sellOrderId
              << " QTY=" << trade.quantity
              << " PRICE=" << trade.price
              << "\n";
}

OrderId MatchingEngine::generateOrderId()
{
    return ++nextOrderId;
}

void MatchingEngine::addLimitOrder(
    const std::string& symbol,
    OrderId id,
    Side side,
    Price price,
    Quantity qty)
{
    OrderBook& book = books[symbol];

    Order incoming{
        id,
        side,
        price,
        qty,
        ++currentTimestamp
    };

    if (side == Side::Buy)
    {
        while (incoming.quantity > 0 &&
               book.hasAsks() &&
               price >= book.getBestAsk())
        {
            Order& best = book.getBestAskOrder();

            Quantity tradeQty = std::min(incoming.quantity, best.quantity);
            
            Trade trade = createTrade(symbol, incoming, best, tradeQty);
            reportTrade(trade);

            incoming.quantity -= tradeQty;
            best.quantity -= tradeQty;

            if (best.quantity == 0)
                book.removeBestAsk();
        }

        if (incoming.quantity > 0)
            book.insertBid(incoming);
    }else
    {
        while (incoming.quantity > 0 &&
               book.hasBids() &&
               price <= book.getBestBid())
        {
            Order& best = book.getBestBidOrder();

            Quantity tradeQty = std::min(incoming.quantity, best.quantity);

            Trade trade = createTrade(symbol, incoming, best, tradeQty);
            reportTrade(trade);

            incoming.quantity -= tradeQty;
            best.quantity -= tradeQty;

            if (best.quantity == 0)
                book.removeBestBid();
        }

        if (incoming.quantity > 0)
            book.insertAsk(incoming);
    }
}

void MatchingEngine::addMarketOrder(
    const std::string& symbol,
    Side side,
    Quantity qty)
{
    OrderBook& book = books[symbol];

    Order incoming{
        generateOrderId(),
        side,
        0,
        qty,
        ++currentTimestamp
    };

    if (side == Side::Buy)
    {
        while (incoming.quantity > 0 && book.hasAsks())
        {
            Order& best = book.getBestAskOrder();

            Quantity tradeQty = std::min(incoming.quantity, best.quantity);

            Trade trade = createTrade(symbol, incoming, best, tradeQty);
            reportTrade(trade);

            incoming.quantity -= tradeQty;
            best.quantity -= tradeQty;

            if (best.quantity == 0)
                book.removeBestAsk();
        }
    }
    else
    {
        while (incoming.quantity > 0 && book.hasBids())
        {
            Order& best = book.getBestBidOrder();

            Quantity tradeQty = std::min(incoming.quantity, best.quantity);

            Trade trade = createTrade(symbol, incoming, best, tradeQty);
            reportTrade(trade);

            incoming.quantity -= tradeQty;
            best.quantity -= tradeQty;

            if (best.quantity == 0)
                book.removeBestBid();
        }
    }
}

bool MatchingEngine::cancelOrder(
    const std::string& symbol,
    OrderId id)
{
    auto it = books.find(symbol);

    if (it == books.end())
        return false;

    return it->second.cancelOrder(id);
}

void MatchingEngine::modifyOrder(
    const std::string& symbol,
    OrderId id,
    Price newPrice,
    Quantity newQty)
{
    auto it = books.find(symbol);
    if (it == books.end())
        return;

    OrderBook& book = it->second;

    Order oldOrder;
    if (!book.getOrder(id, oldOrder))
        return;

    book.cancelOrder(id);

    addLimitOrder(symbol, id, oldOrder.side, newPrice, newQty);
}

void MatchingEngine::printBook(const std::string& symbol) const
{
    auto it = books.find(symbol);
    if (it != books.end())
        it->second.printBook();
}
