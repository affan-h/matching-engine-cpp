#include "matching_engine.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
using namespace std;

inline Timestamp getCurrentTime()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string formatTimestamp(Timestamp ts)
{
    using namespace std::chrono;

    auto sec = duration_cast<std::chrono::seconds>(microseconds(ts));
    auto us  = ts % 1000000;

    std::time_t tt = sec.count();
    std::tm* ptm = std::localtime(&tt);

    std::ostringstream oss;
    oss << std::put_time(ptm, "%Y-%m-%d %H:%M:%S")
        << "." << std::setw(6) << std::setfill('0') << us;

    return oss.str();
}

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
    trade.aggressorSide = incoming.side;
    trade.timestamp = getCurrentTime();
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

void MatchingEngine::logTrade(const Trade& trade)
{
    std::cout << "TRADE "
              << trade.symbol
              << " TID=" << trade.tradeId
              << " TIME=" << formatTimestamp(trade.timestamp)
              << " BUY=" << trade.buyOrderId
              << " SELL=" << trade.sellOrderId
              << " QTY=" << trade.quantity
              << " PRICE=" << trade.price
              << " AGG=" << (trade.aggressorSide == Side::Buy ? "BUY" : "SELL")
              << "\n";
}

OrderId MatchingEngine::generateOrderId()
{
    return ++nextOrderId;
}

OrderId MatchingEngine::addLimitOrder(
    const std::string& symbol,
    Side side,
    Price price,
    Quantity qty)
{
    OrderBook& book = books[symbol];

    OrderId id = generateOrderId();

    Order incoming{
        id,
        side,
        price,
        qty,
        getCurrentTime()
    };

    if (side == Side::Buy)
    {
        while (incoming.quantity > 0 &&
               book.hasAsks() &&
               incoming.price >= book.getBestAsk())
        {
            Order& best = book.getBestAskOrder();

            Quantity tradeQty = std::min(incoming.quantity, best.quantity);
            
            Trade trade = createTrade(symbol, incoming, best, tradeQty);
            logTrade(trade);

            incoming.quantity -= tradeQty;
            best.quantity -= tradeQty;
            book.reduceAskVolume(best.price, tradeQty);

            if (best.quantity == 0)
                book.removeBestAsk();
        }

        if (incoming.quantity > 0)
            book.insertBid(incoming);
    }else
    {
        while (incoming.quantity > 0 &&
               book.hasBids() &&
               incoming.price <= book.getBestBid())
        {
            Order& best = book.getBestBidOrder();

            Quantity tradeQty = std::min(incoming.quantity, best.quantity);

            Trade trade = createTrade(symbol, incoming, best, tradeQty);
            logTrade(trade);

            incoming.quantity -= tradeQty;
            best.quantity -= tradeQty;
            book.reduceBidVolume(best.price, tradeQty);

            if (best.quantity == 0)
                book.removeBestBid();
        }

        if (incoming.quantity > 0)
            book.insertAsk(incoming);
    }
    return id;
}

OrderId MatchingEngine::addMarketOrder(
    const std::string& symbol,
    Side side,
    Quantity qty)
{
    OrderBook& book = books[symbol];

    OrderId id = generateOrderId();

    Order incoming{
        id,
        side,
        0,
        qty,
        getCurrentTime()
    };

    if (side == Side::Buy)
    {
        while (incoming.quantity > 0 && book.hasAsks())
        {
            Order& best = book.getBestAskOrder();

            Quantity tradeQty = std::min(incoming.quantity, best.quantity);

            Trade trade = createTrade(symbol, incoming, best, tradeQty);
            logTrade(trade);

            incoming.quantity -= tradeQty;
            best.quantity -= tradeQty;
            book.reduceAskVolume(best.price, tradeQty);

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
            logTrade(trade);

            incoming.quantity -= tradeQty;
            best.quantity -= tradeQty;
            book.reduceBidVolume(best.price, tradeQty);

            if (best.quantity == 0)
                book.removeBestBid();
        }
    }
    return id;
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

bool MatchingEngine::modifyOrder(
    const std::string& symbol,
    OrderId id,
    Price newPrice,
    Quantity newQty)
{
    auto it = books.find(symbol);
    if (it == books.end())
        return false;

    OrderBook& book = it->second;

    Order oldOrder;
    if (!book.getOrder(id, oldOrder))
        return false;

    book.cancelOrder(id);

    addLimitOrder(symbol, oldOrder.side, newPrice, newQty);
    return true;
}

void MatchingEngine::printOrderBook(const std::string& symbol) const
{
    auto it = books.find(symbol);
    if (it == books.end())
    {
        std::cout << "No such symbol\n";
        return;
    }

    it->second.printBook();
}
