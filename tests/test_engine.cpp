#include <cassert>
#include <iostream>
#include <string>
#include "matching_engine.h"

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

static int passed = 0;
static int failed = 0;

#define TEST(name) void name()
#define RUN(name)  do { \
    try { name(); \
        std::cout << "  PASS  " << #name << "\n"; ++passed; } \
    catch (const std::exception& e) { \
        std::cout << "  FAIL  " << #name << " — " << e.what() << "\n"; ++failed; } \
} while(0)

// Assert with a descriptive message
#define ASSERT(cond, msg) \
    if (!(cond)) throw std::runtime_error(msg)

// ─────────────────────────────────────────────
// Test: Basic limit order rests in book
// ─────────────────────────────────────────────
TEST(test_limit_order_rests_in_book) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::GTC);

    // Book should have a bid at 100
    Order out;
    // We verify indirectly — place a matching sell and confirm trade fires
    uint64_t before = engine.getTotalTrades();
    engine.addLimitOrder(inst, Side::Sell, 100, 10, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == before + 1,
        "Expected one trade after matching sell");
}

// ─────────────────────────────────────────────
// Test: No match when spread exists
// ─────────────────────────────────────────────
TEST(test_no_match_when_spread_exists) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    engine.addLimitOrder(inst, Side::Buy,  100, 10, TimeInForce::GTC);
    engine.addLimitOrder(inst, Side::Sell, 102, 10, TimeInForce::GTC);

    ASSERT(engine.getTotalTrades() == 0,
        "No trade expected when bid=100, ask=102");
}

// ─────────────────────────────────────────────
// Test: Full fill — both orders consumed
// ─────────────────────────────────────────────
TEST(test_full_fill) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    engine.addLimitOrder(inst, Side::Buy,  100, 10, TimeInForce::GTC);
    engine.addLimitOrder(inst, Side::Sell, 100, 10, TimeInForce::GTC);

    ASSERT(engine.getTotalTrades() == 1, "Expected exactly one trade");
    // Both orders fully filled — book should be empty
    // Verify by placing another matching pair and confirming clean state
    uint64_t before = engine.getTotalTrades();
    engine.addLimitOrder(inst, Side::Buy,  100, 5, TimeInForce::GTC);
    engine.addLimitOrder(inst, Side::Sell, 100, 5, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == before + 1,
        "Second matching pair should also produce one trade");
}

// ─────────────────────────────────────────────
// Test: Partial fill — remainder rests
// ─────────────────────────────────────────────
TEST(test_partial_fill_remainder_rests) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    // Buy 10, sell 6 → trade for 6, buy of 4 remains
    engine.addLimitOrder(inst, Side::Buy,  100, 10, TimeInForce::GTC);
    engine.addLimitOrder(inst, Side::Sell, 100,  6, TimeInForce::GTC);

    ASSERT(engine.getTotalTrades() == 1, "Expected one partial fill trade");

    // The remaining buy of 4 should still be in the book
    // Confirm by matching against it
    uint64_t before = engine.getTotalTrades();
    engine.addLimitOrder(inst, Side::Sell, 100, 4, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == before + 1,
        "Remaining qty=4 should still rest and match");
}

// ─────────────────────────────────────────────
// Test: Price priority — best bid matched first
// ─────────────────────────────────────────────
TEST(test_price_priority) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    // Two resting buys at different prices
    engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::GTC);
    engine.addLimitOrder(inst, Side::Buy, 101, 10, TimeInForce::GTC);

    uint64_t before = engine.getTotalTrades();

    // Sell at 100 — should match against the better bid (101) first
    engine.addLimitOrder(inst, Side::Sell, 100, 10, TimeInForce::GTC);

    ASSERT(engine.getTotalTrades() == before + 1, "Expected one trade");

    // Bid at 100 should still be resting (bid at 101 was consumed)
    before = engine.getTotalTrades();
    engine.addLimitOrder(inst, Side::Sell, 100, 10, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == before + 1,
        "Bid at 100 should remain after higher bid was consumed");
}

// ─────────────────────────────────────────────
// Test: FIFO — same price, earlier order filled first
// ─────────────────────────────────────────────
TEST(test_fifo_same_price) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    // Two buys at same price — first in should be first out
    OrderId first  = engine.addLimitOrder(inst, Side::Buy, 100, 5, TimeInForce::GTC);
    OrderId second = engine.addLimitOrder(inst, Side::Buy, 100, 5, TimeInForce::GTC);

    // Sell qty=5 — should consume 'first' entirely, leave 'second'
    engine.addLimitOrder(inst, Side::Sell, 100, 5, TimeInForce::GTC);

    ASSERT(engine.getTotalTrades() == 1, "Expected one trade");

    // 'second' should still be cancellable (still in book)
    bool cancelled = engine.cancelOrder(inst, second);
    ASSERT(cancelled, "Second order should still be in book (FIFO: first was filled)");

    // 'first' should be gone (was filled)
    bool cancelled_first = engine.cancelOrder(inst, first);
    ASSERT(!cancelled_first, "First order should be gone (was filled)");
}

// ─────────────────────────────────────────────
// Test: Cancel removes order from book
// ─────────────────────────────────────────────
TEST(test_cancel_order) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    OrderId id = engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::GTC);
    bool ok = engine.cancelOrder(inst, id);
    ASSERT(ok, "Cancel should return true for a live order");

    // Cancelled order should not match
    uint64_t before = engine.getTotalTrades();
    engine.addLimitOrder(inst, Side::Sell, 100, 10, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == before,
        "Cancelled order should not participate in matching");
}

// ─────────────────────────────────────────────
// Test: Cancel non-existent order returns false
// ─────────────────────────────────────────────
TEST(test_cancel_nonexistent) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    bool ok = engine.cancelOrder(inst, 99999);
    ASSERT(!ok, "Cancel of unknown order should return false");
}

// ─────────────────────────────────────────────
// Test: Market order matches against resting limit
// ─────────────────────────────────────────────
TEST(test_market_order_matches) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    engine.addLimitOrder(inst, Side::Sell, 100, 10, TimeInForce::GTC);

    uint64_t before = engine.getTotalTrades();
    engine.addMarketOrder(inst, Side::Buy, 10);
    ASSERT(engine.getTotalTrades() == before + 1,
        "Market buy should match resting sell");
}

// ─────────────────────────────────────────────
// Test: Market order on empty book — no crash
// ─────────────────────────────────────────────
TEST(test_market_order_empty_book) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    // Should not crash or throw
    engine.addMarketOrder(inst, Side::Buy, 10);
    ASSERT(engine.getTotalTrades() == 0,
        "Market order on empty book should produce no trades");
}

// ─────────────────────────────────────────────
// Test: IOC order — residual discarded
// ─────────────────────────────────────────────
TEST(test_ioc_residual_discarded) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    // Resting sell of qty 5
    engine.addLimitOrder(inst, Side::Sell, 100, 5, TimeInForce::GTC);

    // IOC buy of qty 10 — should fill 5, discard remaining 5
    engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::IOC);

    ASSERT(engine.getTotalTrades() == 1, "IOC should produce one trade");

    // The residual 5 should NOT be in the book
    // If it were, a sell would match it — verify it doesn't
    uint64_t before = engine.getTotalTrades();
    engine.addLimitOrder(inst, Side::Sell, 100, 5, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == before,
        "IOC residual should not rest in book");
}

// ─────────────────────────────────────────────
// Test: IOC order — no liquidity, fully discarded
// ─────────────────────────────────────────────
TEST(test_ioc_no_liquidity) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    // IOC buy with nothing to match against
    engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::IOC);

    ASSERT(engine.getTotalTrades() == 0,
        "IOC with no liquidity should produce no trades and not rest");
}

// ─────────────────────────────────────────────
// Test: Multi-level matching sweeps price levels
// ─────────────────────────────────────────────
TEST(test_multi_level_match) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    // Three resting sells at ascending prices
    engine.addLimitOrder(inst, Side::Sell, 100, 5, TimeInForce::GTC);
    engine.addLimitOrder(inst, Side::Sell, 101, 5, TimeInForce::GTC);
    engine.addLimitOrder(inst, Side::Sell, 102, 5, TimeInForce::GTC);

    // Aggressive buy sweeps all three levels
    engine.addLimitOrder(inst, Side::Buy, 102, 15, TimeInForce::GTC);

    ASSERT(engine.getTotalTrades() == 3,
        "Should produce 3 trades sweeping 3 price levels");
}

// ─────────────────────────────────────────────
// Test: Multiple instruments are independent
// ─────────────────────────────────────────────
TEST(test_multi_instrument_isolation) {
    MatchingEngine engine;
    InstrumentId aapl = engine.registerInstrument("AAPL");
    InstrumentId reli = engine.registerInstrument("RELIANCE");

    // Place a buy on AAPL
    engine.addLimitOrder(aapl, Side::Buy, 150, 10, TimeInForce::GTC);

    // Place a sell on RELIANCE at same price — should NOT match AAPL
    uint64_t before = engine.getTotalTrades();
    engine.addLimitOrder(reli, Side::Sell, 150, 10, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == before,
        "Orders on different instruments must not cross-match");
}

// ─────────────────────────────────────────────
// Test: Modify order — size down keeps priority
// ─────────────────────────────────────────────
TEST(test_modify_size_down_keeps_priority) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    // Two buys at same price
    OrderId first  = engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::GTC);
    OrderId second = engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::GTC);

    // Size down 'first' — should keep queue position
    bool ok = engine.modifyOrder(inst, first, 100, 5);
    ASSERT(ok, "Size-down modify should succeed");

    // Sell qty=5 should still hit 'first' (still at head of queue)
    engine.addLimitOrder(inst, Side::Sell, 100, 5, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == 1, "First order should still be at queue head");

    // 'second' should still be cancellable
    bool cancelled = engine.cancelOrder(inst, second);
    ASSERT(cancelled, "Second order should still be in book");
}

// ─────────────────────────────────────────────
// Test: FOK — full fill available, executes
// ─────────────────────────────────────────────
TEST(test_fok_full_fill_executes) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    // Resting sell of qty 10
    engine.addLimitOrder(inst, Side::Sell, 100, 10, TimeInForce::GTC);

    // FOK buy of qty 10 — full fill available, should execute
    uint64_t before = engine.getTotalTrades();
    engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::FOK);
    ASSERT(engine.getTotalTrades() == before + 1,
        "FOK should execute when full fill is available");
}

// ─────────────────────────────────────────────
// Test: FOK — partial fill only, entire order cancelled
// ─────────────────────────────────────────────
TEST(test_fok_partial_fill_cancelled) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    // Only qty 5 available, FOK needs qty 10
    engine.addLimitOrder(inst, Side::Sell, 100, 5, TimeInForce::GTC);

    uint64_t before = engine.getTotalTrades();
    engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::FOK);

    // No trades should have executed
    ASSERT(engine.getTotalTrades() == before,
        "FOK should not execute if full fill unavailable");

    // The resting sell of qty 5 should still be intact
    engine.addLimitOrder(inst, Side::Buy, 100, 5, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == before + 1,
        "Resting sell should be untouched after FOK rejection");
}

// ─────────────────────────────────────────────
// Test: FOK — no liquidity, cancelled
// ─────────────────────────────────────────────
TEST(test_fok_no_liquidity) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    uint64_t before = engine.getTotalTrades();
    engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::FOK);

    ASSERT(engine.getTotalTrades() == before,
        "FOK with no liquidity should produce no trades");
}

// ─────────────────────────────────────────────
// Test: Invalid Price / Quantity Rejection
// ─────────────────────────────────────────────
TEST(test_invalid_price_and_quantity_rejections) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");
    OutboundEventQueue queue(64);
    engine.setOutboundQueue(&queue);

    // Price 0
    engine.addLimitOrder(inst, Side::Buy, 0, 10, TimeInForce::GTC, 101ULL);
    events::OutboundEvent e1;
    ASSERT(queue.pop(e1), "Expected rejection event");
    ASSERT(e1.type == events::OutboundEventType::OrderState, "Expected OrderState event");
    ASSERT(e1.order.status == events::OrderStatus::Rejected, "Expected Rejected status");
    ASSERT(e1.order.reject_code == events::RejectCode::InvalidPriceQty, "Expected InvalidPriceQty");
    ASSERT(e1.order.client_order_id == 101ULL, "Expected client_order_id match");

    // Quantity 0
    engine.addLimitOrder(inst, Side::Buy, 100, 0, TimeInForce::GTC, 102ULL);
    events::OutboundEvent e2;
    ASSERT(queue.pop(e2), "Expected rejection event");
    ASSERT(e2.order.status == events::OrderStatus::Rejected, "Expected Rejected status");
    ASSERT(e2.order.reject_code == events::RejectCode::InvalidPriceQty, "Expected InvalidPriceQty");

    // Price > 100000
    engine.addLimitOrder(inst, Side::Buy, 100001, 10, TimeInForce::GTC, 103ULL);
    events::OutboundEvent e3;
    ASSERT(queue.pop(e3), "Expected rejection event");
    ASSERT(e3.order.status == events::OrderStatus::Rejected, "Expected Rejected status");
    ASSERT(e3.order.reject_code == events::RejectCode::InvalidPriceQty, "Expected InvalidPriceQty");

    // Market order quantity 0
    engine.addMarketOrder(inst, Side::Buy, 0, 104ULL);
    events::OutboundEvent e4;
    ASSERT(queue.pop(e4), "Expected rejection event");
    ASSERT(e4.order.status == events::OrderStatus::Rejected, "Expected Rejected status");
    ASSERT(e4.order.reject_code == events::RejectCode::InvalidPriceQty, "Expected InvalidPriceQty");
}

// ─────────────────────────────────────────────
// Test: Cancel Nonexistent Order Rejection Code
// ─────────────────────────────────────────────
TEST(test_cancel_nonexistent_order_rejection) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");
    OutboundEventQueue queue(64);
    engine.setOutboundQueue(&queue);

    engine.cancelOrder(inst, 99999, 201ULL);
    events::OutboundEvent e;
    ASSERT(queue.pop(e), "Expected rejection event");
    ASSERT(e.order.status == events::OrderStatus::Rejected, "Expected Rejected status");
    ASSERT(e.order.reject_code == events::RejectCode::OrderNotFound, "Expected OrderNotFound");
    ASSERT(e.order.client_order_id == 201ULL, "Expected client_order_id match");
}

// ─────────────────────────────────────────────
// Test: OrderBook Bit 63 and Word Boundary Precision
// ─────────────────────────────────────────────
TEST(test_orderbook_bit_63_boundary) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    // Prices on bit 63 of word 0 (63), bit 0 of word 1 (64), bit 63 of word 1 (127), bit 0 of word 2 (128)
    engine.addLimitOrder(inst, Side::Buy, 63, 10, TimeInForce::GTC);
    engine.addLimitOrder(inst, Side::Buy, 64, 10, TimeInForce::GTC);
    engine.addLimitOrder(inst, Side::Buy, 127, 10, TimeInForce::GTC);
    engine.addLimitOrder(inst, Side::Buy, 128, 10, TimeInForce::GTC);

    // Place matching sell orders from highest to lowest
    uint64_t before = engine.getTotalTrades();
    engine.addLimitOrder(inst, Side::Sell, 128, 10, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == before + 1, "Match at price 128");

    engine.addLimitOrder(inst, Side::Sell, 127, 10, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == before + 2, "Match at price 127 (bit 63)");

    engine.addLimitOrder(inst, Side::Sell, 64, 10, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == before + 3, "Match at price 64 (bit 0)");

    engine.addLimitOrder(inst, Side::Sell, 63, 10, TimeInForce::GTC);
    ASSERT(engine.getTotalTrades() == before + 4, "Match at price 63 (bit 63)");
}

// ─────────────────────────────────────────────
// Test: Modify with Invalid Price/Qty Preserves Resting Order
// ─────────────────────────────────────────────
TEST(test_modify_invalid_price_qty_preserves_resting_order) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");
    OutboundEventQueue queue(64);
    engine.setOutboundQueue(&queue);

    auto pop_order_state = [](OutboundEventQueue& q, events::OutboundEvent& out) {
        while (q.pop(out)) {
            if (out.type == events::OutboundEventType::OrderState) return true;
        }
        return false;
    };

    OrderId id = engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::GTC, 501ULL);
    events::OutboundEvent e_new;
    ASSERT(pop_order_state(queue, e_new), "Expected New order event");
    ASSERT(e_new.order.status == events::OrderStatus::New, "Status New");

    // Modify with price 0 -> rejected
    bool ok1 = engine.modifyOrder(inst, id, 0, 10, 502ULL);
    ASSERT(!ok1, "Modify with price 0 must return false");
    events::OutboundEvent e_rej1;
    ASSERT(pop_order_state(queue, e_rej1), "Expected Rejection event");
    ASSERT(e_rej1.order.status == events::OrderStatus::Rejected, "Status Rejected");
    ASSERT(e_rej1.order.reject_code == events::RejectCode::InvalidPriceQty, "RejectCode InvalidPriceQty");

    // Modify with qty 0 -> rejected
    bool ok2 = engine.modifyOrder(inst, id, 100, 0, 503ULL);
    ASSERT(!ok2, "Modify with qty 0 must return false");
    events::OutboundEvent e_rej2;
    ASSERT(pop_order_state(queue, e_rej2), "Expected Rejection event");
    ASSERT(e_rej2.order.status == events::OrderStatus::Rejected, "Status Rejected");
    ASSERT(e_rej2.order.reject_code == events::RejectCode::InvalidPriceQty, "RejectCode InvalidPriceQty");

    // Modify with price > 100000 -> rejected
    bool ok3 = engine.modifyOrder(inst, id, 100001, 10, 504ULL);
    ASSERT(!ok3, "Modify with price > 100000 must return false");
    events::OutboundEvent e_rej3;
    ASSERT(pop_order_state(queue, e_rej3), "Expected Rejection event");
    ASSERT(e_rej3.order.status == events::OrderStatus::Rejected, "Status Rejected");
    ASSERT(e_rej3.order.reject_code == events::RejectCode::InvalidPriceQty, "RejectCode InvalidPriceQty");

    // Verify original order at price 100 was NOT cancelled and still matches!
    uint64_t before_trades = engine.getTotalTrades();
    engine.addLimitOrder(inst, Side::Sell, 100, 10, TimeInForce::GTC, 505ULL);
    ASSERT(engine.getTotalTrades() == before_trades + 1, "Resting order should still be intact and match");
}

// ─────────────────────────────────────────────
// Test: Rapid Order Lifecycle Churn
// ─────────────────────────────────────────────
TEST(test_order_lifecycle_rapid_churn) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");

    std::vector<OrderId> active_bids;
    active_bids.reserve(500);

    // 1. Insert 500 bids at price 100
    for (int i = 0; i < 500; ++i) {
        OrderId id = engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::GTC);
        active_bids.push_back(id);
    }

    // 2. Reduce size in-place for every even order
    for (size_t i = 0; i < active_bids.size(); i += 2) {
        bool ok = engine.modifyOrder(inst, active_bids[i], 100, 5);
        ASSERT(ok, "In-place reduce size must succeed");
    }

    // 3. Cancel every fourth order
    size_t cancelled_count = 0;
    for (size_t i = 0; i < active_bids.size(); i += 4) {
        bool ok = engine.cancelOrder(inst, active_bids[i]);
        ASSERT(ok, "Cancel active order must succeed");
        cancelled_count++;
    }
    ASSERT(cancelled_count == 125, "Must have cancelled 125 orders");

    // 4. Fill remaining resting orders with matching market sell orders
    uint64_t before_trades = engine.getTotalTrades();
    engine.addMarketOrder(inst, Side::Sell, 10000);

    ASSERT(engine.getTotalTrades() > before_trades, "Must have executed fills against resting bids");
}

// ─────────────────────────────────────────────
// Test: Matching Engine Operational Telemetry Counters
// ─────────────────────────────────────────────
TEST(test_engine_telemetry_counters) {
    MatchingEngine engine;
    InstrumentId inst = engine.registerInstrument("AAPL");
    OutboundEventQueue queue(64);
    engine.setOutboundQueue(&queue);

    ASSERT(engine.getTotalOrdersAccepted() == 0, "Initial accepted 0");
    ASSERT(engine.getTotalOrdersRejected() == 0, "Initial rejected 0");
    ASSERT(engine.getTotalOrdersCancelled() == 0, "Initial cancelled 0");
    ASSERT(engine.getTotalVolume() == 0, "Initial volume 0");

    // 1. Submit 2 valid limit orders
    OrderId b1 = engine.addLimitOrder(inst, Side::Buy, 100, 10, TimeInForce::GTC, 101ULL);
    OrderId b2 = engine.addLimitOrder(inst, Side::Buy, 100, 20, TimeInForce::GTC, 102ULL);
    (void)b2;
    ASSERT(engine.getTotalOrdersAccepted() == 2, "Accepted 2 orders");

    // 2. Submit invalid orders (price 0, qty 0)
    engine.addLimitOrder(inst, Side::Buy, 0, 10, TimeInForce::GTC, 103ULL);
    engine.addMarketOrder(inst, Side::Buy, 0, 104ULL);
    ASSERT(engine.getTotalOrdersRejected() == 2, "Rejected 2 orders");

    // 3. Cancel an order
    bool c_ok = engine.cancelOrder(inst, b1, 105ULL);
    ASSERT(c_ok, "Cancel b1 succeeded");
    ASSERT(engine.getTotalOrdersCancelled() == 1, "Cancelled 1 order");

    // 4. Match with a market order
    engine.addMarketOrder(inst, Side::Sell, 15, 106ULL);
    ASSERT(engine.getTotalOrdersAccepted() == 3, "Accepted market sell");
    ASSERT(engine.getTotalTrades() == 1, "Executed 1 trade");
    ASSERT(engine.getTotalVolume() == 15, "Volume 15");
    ASSERT(engine.getLastSequence() > 0, "Sequence monotonically progressed");
}

// ─────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────
int main() {
    std::cout << "\n===== Matching Engine Test Suite =====\n\n";

    RUN(test_limit_order_rests_in_book);
    RUN(test_no_match_when_spread_exists);
    RUN(test_full_fill);
    RUN(test_partial_fill_remainder_rests);
    RUN(test_price_priority);
    RUN(test_fifo_same_price);
    RUN(test_cancel_order);
    RUN(test_cancel_nonexistent);
    RUN(test_market_order_matches);
    RUN(test_market_order_empty_book);
    RUN(test_ioc_residual_discarded);
    RUN(test_ioc_no_liquidity);
    RUN(test_multi_level_match);
    RUN(test_multi_instrument_isolation);
    RUN(test_modify_size_down_keeps_priority);
    RUN(test_fok_full_fill_executes);
    RUN(test_fok_partial_fill_cancelled);
    RUN(test_fok_no_liquidity);
    RUN(test_invalid_price_and_quantity_rejections);
    RUN(test_cancel_nonexistent_order_rejection);
    RUN(test_orderbook_bit_63_boundary);
    RUN(test_modify_invalid_price_qty_preserves_resting_order);
    RUN(test_order_lifecycle_rapid_churn);
    RUN(test_engine_telemetry_counters);

    std::cout << "\n======================================\n";
    std::cout << "Results: " << passed << " passed, "
              << failed << " failed\n\n";

    return failed > 0 ? 1 : 0;
}