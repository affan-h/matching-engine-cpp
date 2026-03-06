#include <iostream>
#include <random>
#include "exchange.h"

using namespace std;

int main() {

    Exchange ex;

    random_device rd;
    mt19937 gen(rd());

    uniform_int_distribution<> priceDist(95,105);
    uniform_int_distribution<> qtyDist(1,20);
    uniform_int_distribution<> sideDist(0,1);

    cout << "Running order simulation...\n";

    for(int i=0;i<1000;i++)
    {
        bool isBuy = sideDist(gen);
        int price = priceDist(gen);
        int qty = qtyDist(gen);

        ex.addLimitOrder("AAPL", i, isBuy, price, qty);
    }

    cout << "\nFinal Order Book\n";

    ex.printBook("AAPL");

    return 0;
}