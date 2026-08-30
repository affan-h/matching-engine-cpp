#include "matching_engine.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <algorithm>

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

void MatchingEngine::publishOutbound(const events::OutboundEvent& event)
{
    if (!outboundQueue) return;

    if (event.type == events::OutboundEventType::L2Update) {
        // Coalescing policy for L2 snapshots: non-blocking push
        outboundQueue->push(event);
        return;
    }

    // Bounded retries for Trade and OrderState execution events
    for (int retry = 0; retry < 100; ++retry) {
        if (outboundQueue->push(event)) return;
        std::this_thread::yield();
    }
    outboundQueue->push(event);
}

void MatchingEngine::emitOrderState(
    OrderId id,
    uint64_t client_order_id,
    InstrumentId inst,
    Side side,
    Price price,
    Quantity orig_qty,
    Quantity rem_qty,
    Quantity filled_qty,
    events::OrderStatus status,
    events::RejectCode reject_code,
    Timestamp ts)
{
    if (!outboundQueue) return;

    events::OutboundEvent evt;
    evt.type = events::OutboundEventType::OrderState;
    evt.order.order_id = id;
    evt.order.client_order_id = client_order_id;
    evt.order.instrument_id = inst;
    evt.order.side = side;
    evt.order.price = price;
    evt.order.original_qty = orig_qty;
    evt.order.remaining_qty = rem_qty;
    evt.order.filled_qty = filled_qty;
    evt.order.status = status;
    evt.order.reject_code = reject_code;
    evt.order.timestamp = ts;
    evt.order.sequence = ++globalSequence;

    publishOutbound(evt);
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

    // Notify trade subscribers (e.g. stats tracker)
    for (auto& cb : tradeSubscribers)
        cb(instrument, registry.getSymbol(instrument),
           trade.price, trade.quantity, trade.aggressorSide);

    if (outboundQueue) {
        events::OutboundEvent evt;
        evt.type = events::OutboundEventType::Trade;
        evt.trade.trade_id = trade.tradeId;
        evt.trade.instrument_id = instrument;
        evt.trade.buy_order_id = trade.buyOrderId;
        evt.trade.sell_order_id = trade.sellOrderId;
        evt.trade.aggressor_side = incoming.side;
        evt.trade.price = trade.price;
        evt.trade.quantity = trade.quantity;
        evt.trade.timestamp = trade.timestamp;
        evt.trade.sequence = ++globalSequence;
        publishOutbound(evt);
    }

    return trade;
}

void MatchingEngine::logTrade([[maybe_unused]] const Trade& trade)
{
}

OrderId MatchingEngine::generateOrderId()
{
    return ++nextOrderId;
}

void MatchingEngine::publishSnapshot(InstrumentId instrument)
{
    if (instrument >= books.size()) return;

    std::vector<PriceLevelSnapshot> bids, asks;
    books[instrument].getDepth(bids, asks, 10);

    feed.publishSnapshot(
        instrument,
        registry.getSymbol(instrument),
        bids,
        asks);

    if (outboundQueue) {
        events::OutboundEvent evt;
        evt.type = events::OutboundEventType::L2Update;
        evt.l2.instrument_id = instrument;
        evt.l2.timestamp = getCurrentTime();
        evt.l2.sequence = ++globalSequence;

        size_t b_count = std::min(bids.size(), events::MAX_L2_DEPTH);
        evt.l2.bid_count = static_cast<uint8_t>(b_count);
        for (size_t i = 0; i < b_count; ++i) {
            evt.l2.bids[i].price = bids[i].price;
            evt.l2.bids[i].quantity = bids[i].volume;
        }

        size_t a_count = std::min(asks.size(), events::MAX_L2_DEPTH);
        evt.l2.ask_count = static_cast<uint8_t>(a_count);
        for (size_t i = 0; i < a_count; ++i) {
            evt.l2.asks[i].price = asks[i].price;
            evt.l2.asks[i].quantity = asks[i].volume;
        }

        publishOutbound(evt);
    }
}

OrderId MatchingEngine::addLimitOrder(
    InstrumentId instrument,
    Side side,
    Price price,
    Quantity qty,
    TimeInForce tif,
    uint64_t client_order_id)
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
        if (available < incoming.quantity) {
            emitOrderState(id, client_order_id, instrument, side, price, qty, 0, 0,
                           events::OrderStatus::Rejected, events::RejectCode::InsufficientLiquidityFOK, incoming.timestamp);
            return id;  // Cannot fully fill — cancel entire order, no trades
        }
    }

    Quantity orig_qty = qty;

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

            if (best.quantity == 0) {
                emitOrderState(best.id, 0, instrument, best.side, best.price, best.quantity + tradeQty, 0, tradeQty,
                               events::OrderStatus::Filled, events::RejectCode::None, trade.timestamp);
                book.removeBestAsk();
            } else {
                emitOrderState(best.id, 0, instrument, best.side, best.price, best.quantity + tradeQty, best.quantity, tradeQty,
                               events::OrderStatus::PartiallyFilled, events::RejectCode::None, trade.timestamp);
            }
        }

        // GTC: rest in book. IOC: residual is discarded.
        if (incoming.quantity > 0 && incoming.tif == TimeInForce::GTC) {
            book.insertBid(incoming);
            if (incoming.quantity == orig_qty) {
                emitOrderState(id, client_order_id, instrument, side, price, orig_qty, incoming.quantity, 0,
                               events::OrderStatus::New, events::RejectCode::None, incoming.timestamp);
            } else {
                emitOrderState(id, client_order_id, instrument, side, price, orig_qty, incoming.quantity, orig_qty - incoming.quantity,
                               events::OrderStatus::PartiallyFilled, events::RejectCode::None, incoming.timestamp);
            }
        } else if (incoming.quantity == 0) {
            emitOrderState(id, client_order_id, instrument, side, price, orig_qty, 0, orig_qty,
                           events::OrderStatus::Filled, events::RejectCode::None, incoming.timestamp);
        } else {
            // IOC residual discarded
            emitOrderState(id, client_order_id, instrument, side, price, orig_qty, 0, orig_qty - incoming.quantity,
                           events::OrderStatus::Cancelled, events::RejectCode::None, incoming.timestamp);
        }
    }
    else
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

            if (best.quantity == 0) {
                emitOrderState(best.id, 0, instrument, best.side, best.price, best.quantity + tradeQty, 0, tradeQty,
                               events::OrderStatus::Filled, events::RejectCode::None, trade.timestamp);
                book.removeBestBid();
            } else {
                emitOrderState(best.id, 0, instrument, best.side, best.price, best.quantity + tradeQty, best.quantity, tradeQty,
                               events::OrderStatus::PartiallyFilled, events::RejectCode::None, trade.timestamp);
            }
        }

        // GTC: rest in book. IOC: residual is discarded.
        if (incoming.quantity > 0 && incoming.tif == TimeInForce::GTC) {
            book.insertAsk(incoming);
            if (incoming.quantity == orig_qty) {
                emitOrderState(id, client_order_id, instrument, side, price, orig_qty, incoming.quantity, 0,
                               events::OrderStatus::New, events::RejectCode::None, incoming.timestamp);
            } else {
                emitOrderState(id, client_order_id, instrument, side, price, orig_qty, incoming.quantity, orig_qty - incoming.quantity,
                               events::OrderStatus::PartiallyFilled, events::RejectCode::None, incoming.timestamp);
            }
        } else if (incoming.quantity == 0) {
            emitOrderState(id, client_order_id, instrument, side, price, orig_qty, 0, orig_qty,
                           events::OrderStatus::Filled, events::RejectCode::None, incoming.timestamp);
        } else {
            // IOC residual discarded
            emitOrderState(id, client_order_id, instrument, side, price, orig_qty, 0, orig_qty - incoming.quantity,
                           events::OrderStatus::Cancelled, events::RejectCode::None, incoming.timestamp);
        }
    }

    publishSnapshot(instrument);
    return id;
}

OrderId MatchingEngine::addMarketOrder(
    InstrumentId instrument,
    Side side,
    Quantity qty,
    uint64_t client_order_id)
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

    Quantity orig_qty = qty;

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

            if (best.quantity == 0) {
                emitOrderState(best.id, 0, instrument, best.side, best.price, best.quantity + tradeQty, 0, tradeQty,
                               events::OrderStatus::Filled, events::RejectCode::None, trade.timestamp);
                book.removeBestAsk();
            } else {
                emitOrderState(best.id, 0, instrument, best.side, best.price, best.quantity + tradeQty, best.quantity, tradeQty,
                               events::OrderStatus::PartiallyFilled, events::RejectCode::None, trade.timestamp);
            }
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

            if (best.quantity == 0) {
                emitOrderState(best.id, 0, instrument, best.side, best.price, best.quantity + tradeQty, 0, tradeQty,
                               events::OrderStatus::Filled, events::RejectCode::None, trade.timestamp);
                book.removeBestBid();
            } else {
                emitOrderState(best.id, 0, instrument, best.side, best.price, best.quantity + tradeQty, best.quantity, tradeQty,
                               events::OrderStatus::PartiallyFilled, events::RejectCode::None, trade.timestamp);
            }
        }
    }

    if (incoming.quantity == 0) {
        emitOrderState(id, client_order_id, instrument, side, 0, orig_qty, 0, orig_qty,
                       events::OrderStatus::Filled, events::RejectCode::None, incoming.timestamp);
    } else {
        emitOrderState(id, client_order_id, instrument, side, 0, orig_qty, 0, orig_qty - incoming.quantity,
                       events::OrderStatus::Cancelled, events::RejectCode::None, incoming.timestamp);
    }
    
    publishSnapshot(instrument);
    return id;    
}

bool MatchingEngine::cancelOrder(
    InstrumentId instrument,
    OrderId id,
    uint64_t client_order_id)
{
    if (instrument >= books.size()) {
        emitOrderState(id, client_order_id, instrument, Side::Buy, 0, 0, 0, 0,
                       events::OrderStatus::Rejected, events::RejectCode::UnknownInstrument, getCurrentTime());
        return false;
    }
    Order order;
    bool found = books[instrument].getOrder(id, order);
    bool ok = books[instrument].cancelOrder(id);
    if (ok) {
        emitOrderState(id, client_order_id, instrument, found ? order.side : Side::Buy, found ? order.price : 0,
                       found ? order.quantity : 0, 0, 0,
                       events::OrderStatus::Cancelled, events::RejectCode::None, getCurrentTime());
        publishSnapshot(instrument);
    } else {
        emitOrderState(id, client_order_id, instrument, Side::Buy, 0, 0, 0, 0,
                       events::OrderStatus::Rejected, events::RejectCode::OrderNotFound, getCurrentTime());
    }
    return ok;
}

bool MatchingEngine::modifyOrder(
    InstrumentId instrument,
    OrderId id,
    Price newPrice,
    Quantity newQty,
    uint64_t client_order_id)
{
    if (instrument >= books.size()) {
        emitOrderState(id, client_order_id, instrument, Side::Buy, 0, 0, 0, 0,
                       events::OrderStatus::Rejected, events::RejectCode::UnknownInstrument, getCurrentTime());
        return false;
    }
    OrderBook& book = books[instrument];

    Order oldOrder;
    if (!book.getOrder(id, oldOrder)) {
        emitOrderState(id, client_order_id, instrument, Side::Buy, 0, 0, 0, 0,
                       events::OrderStatus::Rejected, events::RejectCode::OrderNotFound, getCurrentTime());
        return false;
    }

    // IN-PLACE MODIFY: Same price, smaller quantity = keep queue position
    if (newPrice == oldOrder.price && newQty < oldOrder.quantity)
    {
        bool ok = book.reduceOrderSize(id, newQty);
        if (ok) {
            emitOrderState(id, client_order_id, instrument, oldOrder.side, newPrice, oldOrder.quantity, newQty, 0,
                           events::OrderStatus::New, events::RejectCode::None, getCurrentTime());
            publishSnapshot(instrument);
        }
        return ok;
    }

    // Otherwise, lose priority: cancel and replace
    book.cancelOrder(id);
    emitOrderState(id, client_order_id, instrument, oldOrder.side, oldOrder.price, oldOrder.quantity, 0, 0,
                   events::OrderStatus::Cancelled, events::RejectCode::None, getCurrentTime());
    addLimitOrder(instrument, oldOrder.side, newPrice, newQty, TimeInForce::GTC, client_order_id);
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
