#include "matching_engine.h"
#include <iostream>
#include <sstream>

using namespace std;

int main()
{
    MatchingEngine engine;

    cout << "=== SIMPLE EXCHANGE CLI ===\n";
    cout << "Commands:\n";
    cout << "BUY AAPL 100 50\n";
    cout << "SELL AAPL 101 20\n";
    cout << "MARKET_BUY AAPL 30\n";
    cout << "MARKET_SELL AAPL 30\n";
    cout << "CANCEL AAPL 100001\n";
    cout << "MODIFY AAPL 100001 105 40\n";
    cout << "PRINT AAPL\n";
    cout << "EXIT\n\n";

    string line;

    while (true)
    {
        cout << "> ";
        getline(cin, line);

        if (line.empty()) continue;

        stringstream ss(line);
        string cmd;
        ss >> cmd;

        if (cmd == "EXIT")
        {
            break;
        }
        else if (cmd == "BUY")
        {
            string symbol;
            int price, qty;

            ss >> symbol >> price >> qty;

            auto id = engine.addLimitOrder(symbol, Side::Buy, price, qty);

            cout << "Order ID: " << id << "\n";
        }
        else if (cmd == "SELL")
        {
            string symbol;
            int price, qty;

            ss >> symbol >> price >> qty;

            auto id = engine.addLimitOrder(symbol, Side::Sell, price, qty);

            cout << "Order ID: " << id << "\n";
        }
        else if (cmd == "MARKET_BUY")
        {
            string symbol;
            int qty;

            ss >> symbol >> qty;

            auto id = engine.addMarketOrder(symbol, Side::Buy, qty);

            cout << "Market Order ID: " << id << "\n";
        }
        else if (cmd == "MARKET_SELL")
        {
            string symbol;
            int qty;

            ss >> symbol >> qty;

            auto id = engine.addMarketOrder(symbol, Side::Sell, qty);

            cout << "Market Order ID: " << id << "\n";
        }
        else if (cmd == "CANCEL")
        {
            string symbol;
            long id;

            ss >> symbol >> id;

            bool ok = engine.cancelOrder(symbol, id);

            cout << "Cancel: " << (ok ? "SUCCESS" : "FAILED") << "\n";
        }
        else if (cmd == "MODIFY")
        {
            string symbol;
            long id;
            int price, qty;

            ss >> symbol >> id >> price >> qty;

            bool ok = engine.modifyOrder(symbol, id, price, qty);

            cout << "Modify: " << (ok ? "SUCCESS" : "FAILED") << "\n";
        }
        else if (cmd == "PRINT")
        {
            string symbol;
            ss >> symbol;

            engine.printBook(symbol);
        }
        else
        {
            cout << "Unknown command\n";
        }
    }

    return 0;
}