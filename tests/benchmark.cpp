#include "matching_engine.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <functional>

using namespace std;
using namespace std::chrono;

static const int N = 500000; // adjust if needed

// ---------------------------------------------
// Utility Timer
// ---------------------------------------------
long long measure(function<void()> func)
{
    auto start = high_resolution_clock::now();
    func();
    auto end = high_resolution_clock::now();

    return duration_cast<nanoseconds>(end - start).count();
}

// ---------------------------------------------
// TEST 1: INSERT ONLY
// ---------------------------------------------
void test_insert()
{
    MatchingEngine engine;

    long long time = measure([&]()
    {
        for (int i = 0; i < N; i++)
        {
            engine.addLimitOrder("AAPL", Side::Buy, 100 + (i % 10), 10);
        }
    });

    cout << "\n[INSERT TEST]\n";
    cout << "Total: " << time / 1e6 << " ms\n";
    cout << "Latency: " << (double)time / N << " ns/order\n";
}

// ---------------------------------------------
// TEST 2: MATCH HEAVY
// ---------------------------------------------
void test_match()
{
    MatchingEngine engine;

    // Pre-fill book
    for (int i = 0; i < N; i++)
    {
        engine.addLimitOrder("AAPL", Side::Sell, 100 + (i % 5), 10);
    }

    long long time = measure([&]()
    {
        for (int i = 0; i < N; i++)
        {
            engine.addMarketOrder("AAPL", Side::Buy, 10);
        }
    });

    cout << "\n[MATCH TEST]\n";
    cout << "Total: " << time / 1e6 << " ms\n";
    cout << "Latency: " << (double)time / N << " ns/order\n";
}

// ---------------------------------------------
// TEST 3: CANCEL HEAVY
// ---------------------------------------------
void test_cancel()
{
    MatchingEngine engine;
    vector<OrderId> ids;

    // Insert
    for (int i = 0; i < N; i++)
    {
        ids.push_back(engine.addLimitOrder("AAPL", Side::Buy, 100, 10));
    }

    long long time = measure([&]()
    {
        for (int i = 0; i < N; i++)
        {
            engine.cancelOrder("AAPL", ids[i]);
        }
    });

    cout << "\n[CANCEL TEST]\n";
    cout << "Total: " << time / 1e6 << " ms\n";
    cout << "Latency: " << (double)time / N << " ns/order\n";
}

// ---------------------------------------------
// TEST 4: MIXED REALISTIC LOAD
// ---------------------------------------------
void test_mixed()
{
    MatchingEngine engine;

    long long time = measure([&]()
    {
        for (int i = 0; i < N; i++)
        {
            if (i % 3 == 0)
                engine.addLimitOrder("AAPL", Side::Buy, 100 + (i % 10), 10);
            else if (i % 3 == 1)
                engine.addLimitOrder("AAPL", Side::Sell, 100 + (i % 10), 10);
            else
                engine.addMarketOrder("AAPL", Side::Buy, 10);
        }
    });

    cout << "\n[MIXED TEST]\n";
    cout << "Total: " << time / 1e6 << " ms\n";
    cout << "Latency: " << (double)time / N << " ns/op\n";
}

// ---------------------------------------------
// MAIN
// ---------------------------------------------
int main()
{
    cout << "===== MATCHING ENGINE BENCHMARK =====\n";

    test_insert();
    test_match();
    test_cancel();
    test_mixed();

    return 0;
}