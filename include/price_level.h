#pragma once

#include <list>
#include "types.h"

struct PriceLevel
{
    Price price;
    Quantity totalVolume;

    Order* head = nullptr;
    Order* tail = nullptr;

    PriceLevel(Price p)
        : price(p), totalVolume(0)
    {}
};