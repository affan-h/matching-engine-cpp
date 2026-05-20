#pragma once
#include <vector>
#include <stdexcept>
#include "types.h"

class OrderPool
{
private:
    std::vector<Order> pool;
    std::vector<Order*> freeList; // ADDED: Stack of available nodes
    size_t nextFree;

public:
    OrderPool(size_t capacity = 2'000'000)
        : pool(capacity), nextFree(0)
    {
        freeList.reserve(capacity); // Prevent dynamic resizing of free list
    }

    inline Order* allocate()
    {
        // 1. Try to reuse a deleted node
        if (__builtin_expect(!freeList.empty(), 1)) 
        {
            Order* node = freeList.back();
            freeList.pop_back();
            return node;
        }

        // 2. Fallback to bump allocator
        if (__builtin_expect(nextFree >= pool.size(), 0))
            throw std::runtime_error("OrderPool exhausted");

        return &pool[nextFree++];
    }

    inline void deallocate(Order* node)
    {
        // Return memory to the pool immediately
        freeList.push_back(node);
    }

    void reset()
    {
        nextFree = 0;
        freeList.clear();
    }
};