#include "matching_engine.h"
#include <iostream>
using namespace std;

void basicTest(MatchingEngine& engine)
{
cout << "\n=== BASIC MATCH TEST ===\n";
auto buyId  = engine.addLimitOrder("AAPL", Side::Buy, 100, 50);
auto sellId = engine.addLimitOrder("AAPL", Side::Sell, 100, 30);

}

void partialFillTest(MatchingEngine& engine)
{
cout << "\n=== PARTIAL FILL TEST ===\n";
auto buyId  = engine.addLimitOrder("AAPL", Side::Buy, 101, 100);
auto sellId = engine.addLimitOrder("AAPL", Side::Sell, 101, 40);

}

void multiLevelTest(MatchingEngine& engine)
{
cout << "\n=== MULTI LEVEL MATCH ===\n";
engine.addLimitOrder("AAPL", Side::Sell, 102, 50);
engine.addLimitOrder("AAPL", Side::Sell, 103, 50);

engine.addLimitOrder("AAPL", Side::Buy, 105, 80);

}

void marketOrderTest(MatchingEngine& engine)
{
cout << "\n=== MARKET ORDER TEST ===\n";

engine.addLimitOrder("AAPL", Side::Sell, 101, 40);
engine.addLimitOrder("AAPL", Side::Sell, 102, 40);

engine.addMarketOrder("AAPL", Side::Buy, 60);

}

void cancellationTest(MatchingEngine& engine)
{
cout << "\n=== CANCEL TEST ===\n";

auto id = engine.addLimitOrder("AAPL", Side::Buy, 99, 50);

bool cancelled = engine.cancelOrder("AAPL", id);
cout << "Cancel result: " << cancelled << "\n";

// Should NOT match because order was cancelled
engine.addLimitOrder("AAPL", Side::Sell, 99, 50);

}

void cancelNonExistentTest(MatchingEngine& engine)
{
cout << "\n=== CANCEL NON-EXISTENT TEST ===\n";

bool cancelled = engine.cancelOrder("AAPL", 999999);
cout << "Cancel result (should be 0): " << cancelled << "\n";

}

void modifyTest(MatchingEngine& engine)
{
cout << "\n=== MODIFY TEST ===\n";

auto id = engine.addLimitOrder("AAPL", Side::Buy, 98, 40);

engine.modifyOrder("AAPL", id, 102, 40);

// Should trigger trade
engine.addLimitOrder("AAPL", Side::Sell, 101, 40);

}

void modifyNonExistentTest(MatchingEngine& engine)
{
cout << "\n=== MODIFY NON-EXISTENT TEST ===\n";

engine.modifyOrder("AAPL", 999999, 105, 50); // should safely do nothing

}

void fifoTest(MatchingEngine& engine)
{
cout << "\n=== FIFO TEST ===\n";

auto b1 = engine.addLimitOrder("AAPL", Side::Buy, 100, 30);
auto b2 = engine.addLimitOrder("AAPL", Side::Buy, 100, 30);

engine.addLimitOrder("AAPL", Side::Sell, 100, 50);

// Expected:
// First 30 fills b1
// Next 20 fills b2

}

void stressTest(MatchingEngine& engine)
{
cout << "\n=== STRESS TEST ===\n";

// Many buys
for (int i = 0; i < 50; i++)
{
    engine.addLimitOrder("AAPL", Side::Buy, 95 + (i % 5), 10);
}

// Many sells
for (int i = 0; i < 50; i++)
{
    engine.addLimitOrder("AAPL", Side::Sell, 95 + (i % 5), 10);
}

// Large market order
engine.addMarketOrder("AAPL", Side::Buy, 200);

}

int main()
{
{
MatchingEngine engine;
basicTest(engine);
}

{
    MatchingEngine engine;
    partialFillTest(engine);
}

{
    MatchingEngine engine;
    multiLevelTest(engine);
}

{
    MatchingEngine engine;
    marketOrderTest(engine);
}

{
    MatchingEngine engine;
    cancellationTest(engine);
}

{
    MatchingEngine engine;
    cancelNonExistentTest(engine);
}

{
    MatchingEngine engine;
    modifyTest(engine);
}

{
    MatchingEngine engine;
    modifyNonExistentTest(engine);
}

{
    MatchingEngine engine;
    fifoTest(engine);
}

{
    MatchingEngine engine;
    stressTest(engine);
}

return 0;

}
