#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include "types.h"

class SymbolRegistry {
    std::unordered_map<std::string, InstrumentId> symbolToId;
    std::vector<std::string>                      idToSymbol;

public:
    // Register a new symbol, returns its assigned InstrumentId
    InstrumentId registerSymbol(const std::string& symbol) {
        auto it = symbolToId.find(symbol);
        if (it != symbolToId.end())
            return it->second;

        InstrumentId id = static_cast<InstrumentId>(idToSymbol.size());
        symbolToId[symbol] = id;
        idToSymbol.push_back(symbol);
        return id;
    }

    // Lookup by name → id
    InstrumentId getId(const std::string& symbol) const {
        auto it = symbolToId.find(symbol);
        if (it == symbolToId.end())
            throw std::runtime_error("Unknown symbol: " + symbol);
        return it->second;
    }

    // Lookup by id → name
    const std::string& getSymbol(InstrumentId id) const {
        if (id >= idToSymbol.size())
            throw std::runtime_error("Unknown instrument id");
        return idToSymbol[id];
    }

    bool exists(const std::string& symbol) const {
        return symbolToId.count(symbol) > 0;
    }

    size_t count() const { return idToSymbol.size(); }
};