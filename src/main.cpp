#include <iostream>
#include "matching_engine.h"

using namespace std;

int main()
{
    MatchingEngine engine;

    std::string symbol = "AAPL";

    engine.addLimitOrder(symbol, 1, Side::Buy, 100, 50);
    engine.addLimitOrder(symbol, 2, Side::Buy, 101, 30);

    engine.addLimitOrder(symbol, 3, Side::Sell, 102, 40);
    engine.addLimitOrder(symbol, 4, Side::Sell, 101, 20);

    engine.addLimitOrder(symbol, 5, Side::Sell, 100, 25);

    engine.addMarketOrder(symbol, Side::Buy, 60);

    return 0;
}