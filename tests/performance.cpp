#include <iostream>
#include <chrono>
#include "matching_engine.h"

using namespace std;

int main() {

    MatchingEngine ex;

    int N = 100000;

    auto start = chrono::high_resolution_clock::now();

    for(int i=0;i<N;i++)
    {
        ex.addLimitOrder("AAPL", i, Side::Buy, 100 + (i % 5), 10);
    }

    auto end = chrono::high_resolution_clock::now();

    auto duration = chrono::duration_cast<chrono::milliseconds>(end-start);

    cout << "Processed " << N << " orders in "
         << duration.count() << " ms\n";

    return 0;
}

//Processed 100000 orders in 121 ms