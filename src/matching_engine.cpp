#include "matching_engine.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

inline Timestamp getCurrentTime()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string formatTimestamp(Timestamp ts)
{
    using namespace std::chrono;

    auto sec = duration_cast<std::chrono::seconds>(std::chrono::microseconds(ts));
    auto us  = ts % 1000000;

    std::time_t tt = sec.count();
    std::tm* ptm = std::localtime(&tt);

    std::ostringstream oss;
    oss << std::put_time(ptm, "%Y-%m-%d %H:%M:%S")
        << "." << std::setw(6) << std::setfill('0') << us;

    return oss.str();
}

Trade MatchingEngine::createTrade(
    InstrumentId instrument,
    const Order& incoming,
    const Order& resting,
    Quantity qty)
{
    Trade trade;

    trade.instrument = instrument;
    trade.price = resting.price;
    trade.quantity = qty;
    trade.aggressorSide = incoming.side;
    trade.timestamp = getCurrentTime();
    trade.tradeId = ++nextTradeId;
    ++totalTrades;

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

void MatchingEngine::logTrade([[maybe_unused]] const Trade& trade)
{
    // std::cout << "TRADE "
    //           << "INST=" << trade.instrument
    //           << " TID=" << trade.tradeId
    //           << " TIME=" << formatTimestamp(trade.timestamp)
    //           << " BUY=" << trade.buyOrderId
    //           << " SELL=" << trade.sellOrderId
    //           << " QTY=" << trade.quantity
    //           << " PRICE=" << trade.price
    //           << " AGG=" << (trade.aggressorSide == Side::Buy ? "BUY" : "SELL")
    //           << "\n";
}

OrderId MatchingEngine::generateOrderId()
{
    return ++nextOrderId;
}

void MatchingEngine::publishSnapshot(InstrumentId instrument)
{
    if (instrument >= books.size()) return;

    std::vector<PriceLevelSnapshot> bids, asks;
    books[instrument].getDepth(bids, asks, 5);

    feed.publishSnapshot(
        instrument,
        registry.getSymbol(instrument),
        bids,
        asks);
}

OrderId MatchingEngine::addLimitOrder(
    InstrumentId instrument,
    Side side,
    Price price,
    Quantity qty,
    TimeInForce tif = TimeInForce::GTC)
{
    if (__builtin_expect(instrument >= books.size(), 0)) {
        books.resize(instrument + 1);
    }
    OrderBook& book = books[instrument];

    OrderId id = generateOrderId();

    Order incoming{
        id,
        side,
        price,
        qty,
        getCurrentTime(),
        tif
    };

    // FOK: check full fill is possible before executing any trades
    if (incoming.tif == TimeInForce::FOK)
    {
        Quantity available = book.getAvailableVolume(side, price);
        if (available < incoming.quantity)
            return id;  // Cannot fully fill — cancel entire order, no trades
    }

    if (side == Side::Buy)
{
    while (incoming.quantity > 0 &&
           book.hasAsks() &&
           incoming.price >= book.getBestAsk())
    {
        Order& best = book.getBestAskOrder();
        Quantity tradeQty = std::min(incoming.quantity, best.quantity);
        Trade trade = createTrade(instrument, incoming, best, tradeQty);
        logTrade(trade);
        incoming.quantity -= tradeQty;
        best.quantity -= tradeQty;
        book.reduceAskVolume(best.price, tradeQty);
        if (best.quantity == 0)
            book.removeBestAsk();
    }

    // GTC: rest in book. IOC: residual is silently discarded.
    if (incoming.quantity > 0 && incoming.tif == TimeInForce::GTC) {
        book.insertBid(incoming);
    }

} else
{
    while (incoming.quantity > 0 &&
           book.hasBids() &&
           incoming.price <= book.getBestBid())
    {
        Order& best = book.getBestBidOrder();
        Quantity tradeQty = std::min(incoming.quantity, best.quantity);
        Trade trade = createTrade(instrument, incoming, best, tradeQty);
        logTrade(trade);
        incoming.quantity -= tradeQty;
        best.quantity -= tradeQty;
        book.reduceBidVolume(best.price, tradeQty);
        if (best.quantity == 0)
            book.removeBestBid();
    }

    // GTC: rest in book. IOC: residual is silently discarded.
    if (incoming.quantity > 0 && incoming.tif == TimeInForce::GTC) {
        book.insertAsk(incoming);
    }
}

    publishSnapshot(instrument);
    return id;
}

OrderId MatchingEngine::addMarketOrder(
    InstrumentId instrument,
    Side side,
    Quantity qty)
{
    if (instrument >= books.size()) {
        books.resize(instrument + 1);
    }
    OrderBook& book = books[instrument];

    OrderId id = generateOrderId();

    Order incoming{
        id,
        side,
        0,
        qty,
        getCurrentTime(),
        TimeInForce::IOC
    };

    if (side == Side::Buy)
    {
        while (incoming.quantity > 0 && book.hasAsks())
        {
            Order& best = book.getBestAskOrder();

            Quantity tradeQty = std::min(incoming.quantity, best.quantity);

            Trade trade = createTrade(instrument, incoming, best, tradeQty);
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

            Trade trade = createTrade(instrument, incoming, best, tradeQty);
            logTrade(trade);

            incoming.quantity -= tradeQty;
            best.quantity -= tradeQty;
            book.reduceBidVolume(best.price, tradeQty);

            if (best.quantity == 0)
                book.removeBestBid();
        }
    }
    
    publishSnapshot(instrument);
    return id;    
}

bool MatchingEngine::cancelOrder(
    InstrumentId instrument,
    OrderId id)
{
    // Fast bounds check instead of map lookup
    if (instrument >= books.size()) return false; 
    return books[instrument].cancelOrder(id);
}

bool MatchingEngine::modifyOrder(InstrumentId instrument, OrderId id, Price newPrice, Quantity newQty)
{
    if (instrument >= books.size()) return false;
    OrderBook& book = books[instrument];

    Order oldOrder;
    if (!book.getOrder(id, oldOrder)) return false;

    // IN-PLACE MODIFY: Same price, smaller quantity = keep queue position
    if (newPrice == oldOrder.price && newQty < oldOrder.quantity)
    {
        return book.reduceOrderSize(id, newQty);
    }

    // Otherwise, lose priority: cancel and replace
    book.cancelOrder(id);
    addLimitOrder(instrument, oldOrder.side, newPrice, newQty);
    publishSnapshot(instrument);
    return true;
}

void MatchingEngine::printOrderBook(InstrumentId instrument) const
{
    if (instrument >= books.size())
    {
        std::cout << "No such instrument\n";
        return;
    }

    books[instrument].printBook();
}
