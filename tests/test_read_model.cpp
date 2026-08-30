#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include "read_model.h"
#include "projector.h"
#include "matching_engine.h"
#include "spsc_queue.h"

#define TEST(name) static void name()
#define RUN_TEST(name) \
    do { \
        name(); \
        std::cout << "  PASS  " << #name << "\n"; \
        passed++; \
    } while (0)

static int passed = 0;

// 1. Trade Event Reaches Read Model
TEST(test_trade_event_reaches_read_model) {
    ReadModel model(100, 100);
    model.registerSymbol(0, "AAPL");

    events::OutboundEvent evt;
    evt.type = events::OutboundEventType::Trade;
    evt.trade.trade_id = 1;
    evt.trade.instrument_id = 0;
    evt.trade.buy_order_id = 10;
    evt.trade.sell_order_id = 20;
    evt.trade.aggressor_side = Side::Buy;
    evt.trade.price = 150;
    evt.trade.quantity = 100;
    evt.trade.timestamp = 1000;

    model.applyEvent(evt);

    std::vector<TradeRecord> trades;
    assert(model.getRecentTrades(0, 10, trades));
    assert(trades.size() == 1);
    assert(trades[0].trade_id == 1);
    assert(trades[0].symbol == "AAPL");
    assert(trades[0].price == 150);
    assert(trades[0].quantity == 100);
    assert(trades[0].aggressor_side == Side::Buy);
}

// 2. L2 Update Reaches Read Model
TEST(test_l2_update_reaches_read_model) {
    ReadModel model(100, 100);
    model.registerSymbol(0, "AAPL");

    events::OutboundEvent evt;
    evt.type = events::OutboundEventType::L2Update;
    evt.l2.instrument_id = 0;
    evt.l2.sequence = 5;
    evt.l2.timestamp = 2000;
    evt.l2.bid_count = 2;
    evt.l2.ask_count = 1;
    evt.l2.bids[0] = {150, 10};
    evt.l2.bids[1] = {149, 20};
    evt.l2.asks[0] = {151, 15};

    model.applyEvent(evt);

    L2BookState book;
    assert(model.getL2Book(0, book));
    assert(book.sequence == 5);
    assert(book.symbol == "AAPL");
    assert(book.bid_count == 2);
    assert(book.bids[0].price == 150 && book.bids[0].quantity == 10);
    assert(book.bids[1].price == 149 && book.bids[1].quantity == 20);
    assert(book.ask_count == 1);
    assert(book.asks[0].price == 151 && book.asks[0].quantity == 15);
}

// 3. Order State Lifecycle Tracking
TEST(test_order_state_lifecycle) {
    ReadModel model(100, 100);
    model.registerSymbol(0, "AAPL");

    // 1. Order New
    events::OutboundEvent e1;
    e1.type = events::OutboundEventType::OrderState;
    e1.order.order_id = 42;
    e1.order.instrument_id = 0;
    e1.order.side = Side::Buy;
    e1.order.price = 150;
    e1.order.original_qty = 100;
    e1.order.remaining_qty = 100;
    e1.order.filled_qty = 0;
    e1.order.status = events::OrderStatus::New;
    e1.order.timestamp = 100;
    model.applyEvent(e1);

    OrderRecord rec;
    assert(model.getOrder(42, rec));
    assert(rec.status == events::OrderStatus::New);
    assert(rec.remaining_qty == 100);

    // 2. Order Partial Fill
    events::OutboundEvent e2;
    e2.type = events::OutboundEventType::OrderState;
    e2.order = e1.order;
    e2.order.remaining_qty = 40;
    e2.order.filled_qty = 60;
    e2.order.status = events::OrderStatus::PartiallyFilled;
    model.applyEvent(e2);

    assert(model.getOrder(42, rec));
    assert(rec.status == events::OrderStatus::PartiallyFilled);
    assert(rec.remaining_qty == 40);
    assert(rec.filled_qty == 60);

    // 3. Order Complete Fill
    events::OutboundEvent e3;
    e3.type = events::OutboundEventType::OrderState;
    e3.order = e2.order;
    e3.order.remaining_qty = 0;
    e3.order.filled_qty = 100;
    e3.order.status = events::OrderStatus::Filled;
    model.applyEvent(e3);

    assert(model.getOrder(42, rec));
    assert(rec.status == events::OrderStatus::Filled);
    assert(rec.remaining_qty == 0);
    assert(rec.filled_qty == 100);
}

// 4. Multiple Events Preserve Ordering
TEST(test_multiple_events_preserve_ordering) {
    ReadModel model(100, 100);
    model.registerSymbol(0, "AAPL");

    for (uint64_t i = 1; i <= 5; ++i) {
        events::OutboundEvent evt;
        evt.type = events::OutboundEventType::Trade;
        evt.trade.trade_id = i;
        evt.trade.instrument_id = 0;
        evt.trade.price = 100 + i;
        evt.trade.quantity = i * 10;
        evt.trade.timestamp = i * 1000;
        model.applyEvent(evt);
    }

    std::vector<TradeRecord> trades;
    model.getRecentTrades(0, 10, trades);
    assert(trades.size() == 5);
    // Reverse chronological order: newest first
    assert(trades[0].trade_id == 5);
    assert(trades[1].trade_id == 4);
    assert(trades[2].trade_id == 3);
    assert(trades[3].trade_id == 2);
    assert(trades[4].trade_id == 1);
}

// 5. Multi-Instrument Isolation
TEST(test_multi_instrument_isolation) {
    ReadModel model(100, 100);
    model.registerSymbol(0, "AAPL");
    model.registerSymbol(1, "RELIANCE");

    // Trade on AAPL
    events::OutboundEvent e_aapl;
    e_aapl.type = events::OutboundEventType::Trade;
    e_aapl.trade.trade_id = 1;
    e_aapl.trade.instrument_id = 0;
    e_aapl.trade.price = 150;
    e_aapl.trade.quantity = 10;
    model.applyEvent(e_aapl);

    // Trade on RELIANCE
    events::OutboundEvent e_rel;
    e_rel.type = events::OutboundEventType::Trade;
    e_rel.trade.trade_id = 2;
    e_rel.trade.instrument_id = 1;
    e_rel.trade.price = 2800;
    e_rel.trade.quantity = 5;
    model.applyEvent(e_rel);

    std::vector<TradeRecord> aapl_trades, rel_trades;
    model.getRecentTrades(0, 10, aapl_trades);
    model.getRecentTrades(1, 10, rel_trades);

    assert(aapl_trades.size() == 1 && aapl_trades[0].symbol == "AAPL" && aapl_trades[0].price == 150);
    assert(rel_trades.size() == 1 && rel_trades[0].symbol == "RELIANCE" && rel_trades[0].price == 2800);
}

// 6. Bounded Trade History Eviction
TEST(test_bounded_trade_history_eviction) {
    // Capacity of 10 trades
    ReadModel model(100, 10);
    model.registerSymbol(0, "AAPL");

    // Insert 15 trades
    for (uint64_t i = 1; i <= 15; ++i) {
        events::OutboundEvent evt;
        evt.type = events::OutboundEventType::Trade;
        evt.trade.trade_id = i;
        evt.trade.instrument_id = 0;
        evt.trade.price = 100;
        evt.trade.quantity = 1;
        model.applyEvent(evt);
    }

    assert(model.getTradeCount(0) == 10);

    std::vector<TradeRecord> trades;
    model.getRecentTrades(0, 20, trades);
    assert(trades.size() == 10);
    // Newest is 15, oldest remaining is 6 (1..5 were evicted)
    assert(trades[0].trade_id == 15);
    assert(trades[9].trade_id == 6);
}

// 7. Bounded Order History Eviction
TEST(test_bounded_order_history_eviction) {
    // Capacity of 10 orders
    ReadModel model(10, 100);
    model.registerSymbol(0, "AAPL");

    for (OrderId id = 1; id <= 15; ++id) {
        events::OutboundEvent evt;
        evt.type = events::OutboundEventType::OrderState;
        evt.order.order_id = id;
        evt.order.instrument_id = 0;
        evt.order.price = 100;
        evt.order.original_qty = 10;
        evt.order.status = events::OrderStatus::New;
        model.applyEvent(evt);
    }

    assert(model.getOrderCount() == 10);

    OrderRecord rec;
    // Orders 1..5 should be evicted
    for (OrderId id = 1; id <= 5; ++id) {
        assert(!model.getOrder(id, rec));
    }
    // Orders 6..15 should exist
    for (OrderId id = 6; id <= 15; ++id) {
        assert(model.getOrder(id, rec));
        assert(rec.order_id == id);
    }
}

// 8. Unknown Symbol and Nonexistent Order Query
TEST(test_unknown_symbol_and_nonexistent_order) {
    ReadModel model(100, 100);
    model.registerSymbol(0, "AAPL");

    L2BookState book;
    assert(!model.getL2BookBySymbol("UNKNOWN", book));

    std::vector<TradeRecord> trades;
    assert(!model.getRecentTradesBySymbol("UNKNOWN", 10, trades));

    OrderRecord rec;
    assert(!model.getOrder(999999, rec));
}

// 9. Concurrent Reads with Projector Writes
TEST(test_concurrent_reads_with_projector_writes) {
    ReadModel model(1000, 1000);
    model.registerSymbol(0, "AAPL");
    OutboundEventQueue queue(1024);
    Projector projector(queue, model);
    projector.start();

    std::atomic<bool> stop_flag{false};

    // Writer thread pushing events through queue
    std::thread writer([&]() {
        for (uint64_t i = 1; i <= 500; ++i) {
            events::OutboundEvent evt;
            evt.type = events::OutboundEventType::Trade;
            evt.trade.trade_id = i;
            evt.trade.instrument_id = 0;
            evt.trade.price = 100 + (i % 50);
            evt.trade.quantity = 10;
            while (!queue.push(evt)) {
                std::this_thread::yield();
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        stop_flag.store(true);
    });

    // Multiple concurrent reader threads querying ReadModel
    std::atomic<uint64_t> reads_count{0};
    auto reader_fn = [&]() {
        while (!stop_flag.load()) {
            std::vector<TradeRecord> trades;
            model.getRecentTrades(0, 10, trades);

            L2BookState book;
            model.getL2Book(0, book);

            OrderRecord rec;
            model.getOrder(1, rec);

            reads_count++;
            std::this_thread::yield();
        }
    };

    std::thread r1(reader_fn);
    std::thread r2(reader_fn);

    writer.join();
    r1.join();
    r2.join();

    projector.stop();

    assert(model.getTradeCount(0) == 500);
    assert(reads_count.load() > 0);
}

// 10. Projector Shutdown Drains Queue Completely
TEST(test_projector_shutdown_drain) {
    ReadModel model(1000, 1000);
    model.registerSymbol(0, "AAPL");
    OutboundEventQueue queue(1024);

    // Push 50 events before starting projector
    for (uint64_t i = 1; i <= 50; ++i) {
        events::OutboundEvent evt;
        evt.type = events::OutboundEventType::Trade;
        evt.trade.trade_id = i;
        evt.trade.instrument_id = 0;
        evt.trade.price = 150;
        evt.trade.quantity = 1;
        queue.push(evt);
    }

    Projector projector(queue, model);
    projector.start();
    // Immediate stop should drain all 50 events
    projector.stop();

    assert(model.getTradeCount(0) == 50);
    assert(queue.empty());
}

// 11. End-to-End MatchingEngine -> SPSC Outbound -> Projector -> ReadModel
TEST(test_end_to_end_engine_to_read_model) {
    MatchingEngine engine;
    ReadModel model(1000, 1000);

    InstrumentId aapl = engine.registerInstrument("AAPL");
    model.registerSymbol(aapl, "AAPL");

    OutboundEventQueue outbound_queue(1024);
    engine.setOutboundQueue(&outbound_queue);

    Projector projector(outbound_queue, model);
    projector.start();

    // 1. Submit resting Limit Buy order
    OrderId buy_id = engine.addLimitOrder(aapl, Side::Buy, 150, 10, TimeInForce::GTC);

    // 2. Submit matching Limit Sell order
    OrderId sell_id = engine.addLimitOrder(aapl, Side::Sell, 150, 10, TimeInForce::GTC);

    // Wait briefly for projector to process outbound queue
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify Trade in ReadModel
    std::vector<TradeRecord> trades;
    assert(model.getRecentTrades(aapl, 10, trades));
    assert(trades.size() == 1);
    assert(trades[0].price == 150);
    assert(trades[0].quantity == 10);
    assert(trades[0].buy_order_id == buy_id);
    assert(trades[0].sell_order_id == sell_id);

    // Verify Orders in ReadModel
    OrderRecord buy_rec, sell_rec;
    assert(model.getOrder(buy_id, buy_rec));
    assert(buy_rec.status == events::OrderStatus::Filled);
    assert(buy_rec.filled_qty == 10);
    assert(buy_rec.remaining_qty == 0);

    assert(model.getOrder(sell_id, sell_rec));
    assert(sell_rec.status == events::OrderStatus::Filled);
    assert(sell_rec.filled_qty == 10);
    assert(sell_rec.remaining_qty == 0);

    projector.stop();
}

int main() {
    std::cout << "\n===== Read Model & Projector Test Suite =====\n\n";

    RUN_TEST(test_trade_event_reaches_read_model);
    RUN_TEST(test_l2_update_reaches_read_model);
    RUN_TEST(test_order_state_lifecycle);
    RUN_TEST(test_multiple_events_preserve_ordering);
    RUN_TEST(test_multi_instrument_isolation);
    RUN_TEST(test_bounded_trade_history_eviction);
    RUN_TEST(test_bounded_order_history_eviction);
    RUN_TEST(test_unknown_symbol_and_nonexistent_order);
    RUN_TEST(test_concurrent_reads_with_projector_writes);
    RUN_TEST(test_projector_shutdown_drain);
    RUN_TEST(test_end_to_end_engine_to_read_model);

    std::cout << "\n=============================================\n";
    std::cout << "Results: " << passed << " passed, 0 failed\n\n";

    return 0;
}
