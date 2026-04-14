#pragma once

#include <vector>
#include <stdexcept>
#include "types.h"

class OrderPool
{
private:
    std::vector<Order> pool;
    size_t nextFree;

public:
    OrderPool(size_t capacity = 2'000'000)
        : pool(capacity), nextFree(0)
    {
    }

    inline Order* allocate()
    {
        if (nextFree >= pool.size())
            throw std::runtime_error("OrderPool exhausted");

        return &pool[nextFree++];
    }

    inline void deallocate(Order* node)
    {
        // 🔥 NO-OP for now
        // (we don't reuse memory in bump allocator)
    }

    void reset()
    {
        nextFree = 0;
    }
};