#pragma once

#include <list>
#include "types.h"

struct PriceLevel
{
    Price price;
    Quantity totalVolume;

    std::list<Order> orders;

    PriceLevel(Price p)
        : price(p), totalVolume(0)
    {}
};