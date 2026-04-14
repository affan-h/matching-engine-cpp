#include "matching_engine.h"
#include <iostream>
#include <vector>

using namespace std;

void separator(const string& title)
{
    cout << "\n==================== " << title << " ====================\n";
}

void basicTest()
{
    separator("BASIC MATCH");

    MatchingEngine engine;

    auto id1 = engine.addLimitOrder("AAPL", Side::Buy, 100, 10);
    auto id2 = engine.addLimitOrder("AAPL", Side::Sell, 100, 10);

    engine.printOrderBook("AAPL");
}

void partialFillTest()
{
    separator("PARTIAL FILL");

    MatchingEngine engine;

    engine.addLimitOrder("AAPL", Side::Buy, 100, 20);
    engine.addLimitOrder("AAPL", Side::Sell, 100, 5);

    engine.printOrderBook("AAPL");
}

void multiLevelTest()
{
    separator("MULTI LEVEL MATCH");

    MatchingEngine engine;

    engine.addLimitOrder("AAPL", Side::Sell, 101, 10);
    engine.addLimitOrder("AAPL", Side::Sell, 102, 10);
    engine.addLimitOrder("AAPL", Side::Sell, 103, 10);

    engine.addMarketOrder("AAPL", Side::Buy, 25);

    engine.printOrderBook("AAPL");
}

void marketOrderTest()
{
    separator("MARKET ORDER");

    MatchingEngine engine;

    engine.addLimitOrder("AAPL", Side::Sell, 100, 10);
    engine.addLimitOrder("AAPL", Side::Sell, 101, 10);

    engine.addMarketOrder("AAPL", Side::Buy, 15);

    engine.printOrderBook("AAPL");
}

void cancelTest()
{
    separator("CANCEL ORDER");

    MatchingEngine engine;

    auto id1 = engine.addLimitOrder("AAPL", Side::Buy, 100, 10);
    auto id2 = engine.addLimitOrder("AAPL", Side::Buy, 101, 10);

    engine.cancelOrder("AAPL", id1);

    engine.printOrderBook("AAPL");
}

void modifyTest()
{
    separator("MODIFY ORDER");

    MatchingEngine engine;

    auto id = engine.addLimitOrder("AAPL", Side::Buy, 100, 10);

    engine.modifyOrder("AAPL", id, 105, 10);

    engine.printOrderBook("AAPL");
}

void fifoTest()
{
    separator("FIFO TEST");

    MatchingEngine engine;

    engine.addLimitOrder("AAPL", Side::Buy, 100, 10); // first
    engine.addLimitOrder("AAPL", Side::Buy, 100, 10); // second

    engine.addLimitOrder("AAPL", Side::Sell, 100, 15);

    engine.printOrderBook("AAPL");
}

void stressTest()
{
    separator("STRESS TEST");

    MatchingEngine engine;

    const int N = 100000;
    vector<OrderId> ids;

    // Insert
    for (int i = 0; i < N; i++)
    {    
        ids.push_back(engine.addLimitOrder("AAPL", Side::Buy, 100 + (i % 10), 10));
    }

    // Cancel half
    for (int i = 0; i < N; i += 2)
    {
        engine.cancelOrder("AAPL", ids[i]); // based on your ID scheme
    }

    // Match
    for (int i = 0; i < N; i++)
    {
        engine.addMarketOrder("AAPL", Side::Sell, 5);
    }

    engine.printOrderBook("AAPL");
}

void edgeCaseTest()
{
    separator("EDGE CASES");

    MatchingEngine engine;

    // Cancel non-existent
    engine.cancelOrder("AAPL", 999999);

    // Market order on empty book
    engine.addMarketOrder("AAPL", Side::Buy, 10);

    // Modify non-existent
    engine.modifyOrder("AAPL", 999999, 200, 10);

    engine.printOrderBook("AAPL");
}

int main()
{
    basicTest();
    partialFillTest();
    multiLevelTest();
    marketOrderTest();
    cancelTest();
    modifyTest();
    fifoTest();
    edgeCaseTest();
    stressTest();

    return 0;
}