#include "matching_engine.h"
#include <iostream>
using namespace std;

void basicTest(MatchingEngine& engine)
{
    std::cout << "\n=== BASIC MATCH TEST ===\n";

    engine.addLimitOrder("AAPL", 1, Side::Buy, 100, 50);
    engine.addLimitOrder("AAPL", 2, Side::Sell, 100, 30);
}

void partialFillTest(MatchingEngine& engine)
{
    std::cout << "\n=== PARTIAL FILL TEST ===\n";

    engine.addLimitOrder("AAPL", 3, Side::Buy, 101, 100);
    engine.addLimitOrder("AAPL", 4, Side::Sell, 101, 40);
}

void multiLevelTest(MatchingEngine& engine)
{
    std::cout << "\n=== MULTI LEVEL MATCH ===\n";

    engine.addLimitOrder("AAPL", 5, Side::Sell, 102, 50);
    engine.addLimitOrder("AAPL", 6, Side::Sell, 103, 50);

    engine.addLimitOrder("AAPL", 7, Side::Buy, 105, 80);
}

void marketOrderTest(MatchingEngine& engine)
{
    std::cout << "\n=== MARKET ORDER TEST ===\n";

    engine.addMarketOrder("AAPL", Side::Buy, 60);
}

void cancellationTest(MatchingEngine& engine)
{
    std::cout << "\n=== CANCEL TEST ===\n";

    engine.addLimitOrder("AAPL", 8, Side::Buy, 99, 50);
    engine.cancelOrder("AAPL", 8);

    // Should NOT trade
    engine.addLimitOrder("AAPL", 9, Side::Sell, 99, 50);
}

void modifyTest(MatchingEngine& engine)
{
    std::cout << "\n=== MODIFY TEST ===\n";

    engine.addLimitOrder("AAPL", 9, Side::Buy, 98, 40);
    engine.modifyOrder("AAPL", 9, 102, 40);

    // Should trigger trade
    engine.addLimitOrder("AAPL", 10, Side::Sell, 101, 40);
}

void stressTest(MatchingEngine& engine)
{
    std::cout << "\n=== STRESS TEST ===\n";

    for (int i = 10; i < 100; i++)
    {
        engine.addLimitOrder("AAPL", i, Side::Buy, 95 + (i % 5), 10);
    }

    for (int i = 100; i < 150; i++)
    {
        engine.addLimitOrder("AAPL", i, Side::Sell, 95 + (i % 5), 10);
    }

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
        modifyTest(engine);
    }

    {
        MatchingEngine engine;
        stressTest(engine);
    }

    return 0;
}