#include <iostream>
#include <chrono>
#include "exchange.h"

using namespace std;

int main() {

    Exchange ex;

    int N = 100000;

    auto start = chrono::high_resolution_clock::now();

    for(int i=0;i<N;i++)
    {
        ex.addLimitOrder("AAPL", i, true, 100 + (i % 5), 10);
    }

    auto end = chrono::high_resolution_clock::now();

    auto duration = chrono::duration_cast<chrono::milliseconds>(end-start);

    cout << "Processed " << N << " orders in "
         << duration.count() << " ms\n";

    return 0;
}