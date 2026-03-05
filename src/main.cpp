#include <iostream>
#include "exchange.h"

using namespace std;

int main() {

    Exchange ex;

    cout << "\n===== TEST 1: Basic Limit Matching =====\n";

    ex.addLimitOrder("AAPL", 1, false, 105, 10);  // Sell
    ex.addLimitOrder("AAPL", 2, true, 110, 7);    // Buy

    ex.printTrades("AAPL");
    ex.printBook("AAPL");

    ex.clearEvents("AAPL");


    cout << "\n===== TEST 2: Partial Fill =====\n";

    ex.addLimitOrder("AAPL", 3, true, 100, 5);
    ex.addLimitOrder("AAPL", 4, false, 99, 10);

    ex.printTrades("AAPL");
    ex.printBook("AAPL");

    ex.clearEvents("AAPL");


    cout << "\n===== TEST 3: Market Order =====\n";

    ex.addLimitOrder("AAPL", 5, false, 120, 5);

    ex.addMarketOrder("AAPL", true, 10);

    ex.printTrades("AAPL");
    ex.printBook("AAPL");

    ex.clearEvents("AAPL");


    cout << "\n===== TEST 4: Cancel Order =====\n";

    ex.addLimitOrder("AAPL", 6, true, 130, 10);

    ex.printBook("AAPL");

    ex.cancelOrder("AAPL", 6);

    ex.printBook("AAPL");


    cout << "\n===== TEST 5: Modify Order =====\n";

    ex.addLimitOrder("AAPL", 7, false, 150, 5);
    ex.addLimitOrder("AAPL", 8, true, 140, 5);

    ex.modifyOrder("AAPL", 8, 160, 5);

    ex.printTrades("AAPL");
    ex.printBook("AAPL");

    ex.clearEvents("AAPL");


    cout << "\n===== TEST 6: Best Bid / Ask =====\n";

    cout << "Best Bid: " << ex.getBestBid("AAPL") << endl;
    cout << "Best Ask: " << ex.getBestAsk("AAPL") << endl;


    cout << "\n===== TEST 7: Multi Symbol =====\n";

    ex.addLimitOrder("TSLA", 1, true, 200, 10);
    ex.addLimitOrder("TSLA", 2, false, 195, 5);

    ex.printTrades("TSLA");
    ex.printBook("TSLA");


    cout << "\n===== END TEST =====\n";

    return 0;
}