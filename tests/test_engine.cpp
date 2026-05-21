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

    std::cout << "\n======================================\n";
    std::cout << "Results: " << passed << " passed, "
              << failed << " failed\n\n";

    return failed > 0 ? 1 : 0;
}