#include "read_model.h"
#include <algorithm>

ReadModel::ReadModel(size_t max_orders_cap, size_t max_trades_cap)
    : max_orders(max_orders_cap), max_trades_per_symbol(max_trades_cap) {}

void ReadModel::registerSymbol(InstrumentId id, const std::string& symbol) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    id_to_symbol[id] = symbol;
    symbol_to_id[symbol] = id;
    if (trade_histories.find(id) == trade_histories.end()) {
        trade_histories.emplace(id, BoundedTradeHistory(max_trades_per_symbol));
    }
}

bool ReadModel::getInstrumentId(const std::string& symbol, InstrumentId& out) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    auto it = symbol_to_id.find(symbol);
    if (it != symbol_to_id.end()) {
        out = it->second;
        return true;
    }
    return false;
}

std::string ReadModel::getSymbol(InstrumentId id) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    auto it = id_to_symbol.find(id);
    if (it != id_to_symbol.end()) {
        return it->second;
    }
    return "UNKNOWN";
}

void ReadModel::recordOrderInternal(const OrderRecord& record) {
    auto it = orders.find(record.order_id);
    if (it == orders.end()) {
        // Enforce bounded capacity via FIFO eviction
        while (orders.size() >= max_orders && !order_eviction_queue.empty()) {
            OrderId oldest = order_eviction_queue.front();
            order_eviction_queue.pop_front();
            orders.erase(oldest);
        }
        order_eviction_queue.push_back(record.order_id);
    }
    orders[record.order_id] = record;
}

void ReadModel::applyEvent(const events::OutboundEvent& event) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);

    switch (event.type) {
        case events::OutboundEventType::Trade: {
            const auto& p = event.trade;
            std::string sym = "";
            auto sym_it = id_to_symbol.find(p.instrument_id);
            if (sym_it != id_to_symbol.end()) sym = sym_it->second;

            TradeRecord tr;
            tr.trade_id = p.trade_id;
            tr.instrument_id = p.instrument_id;
            tr.symbol = sym;
            tr.buy_order_id = p.buy_order_id;
            tr.sell_order_id = p.sell_order_id;
            tr.aggressor_side = p.aggressor_side;
            tr.price = p.price;
            tr.quantity = p.quantity;
            tr.timestamp = p.timestamp;

            auto it = trade_histories.find(p.instrument_id);
            if (it == trade_histories.end()) {
                it = trade_histories.emplace(p.instrument_id, BoundedTradeHistory(max_trades_per_symbol)).first;
            }
            it->second.add(tr);
            break;
        }

        case events::OutboundEventType::L2Update: {
            const auto& p = event.l2;
            std::string sym = "";
            auto sym_it = id_to_symbol.find(p.instrument_id);
            if (sym_it != id_to_symbol.end()) sym = sym_it->second;

            L2BookState state;
            state.instrument_id = p.instrument_id;
            state.symbol = sym;
            state.sequence = p.sequence;
            state.timestamp = p.timestamp;
            state.bid_count = p.bid_count;
            state.ask_count = p.ask_count;
            for (size_t i = 0; i < events::MAX_L2_DEPTH; ++i) {
                state.bids[i] = p.bids[i];
                state.asks[i] = p.asks[i];
            }
            l2_books[p.instrument_id] = state;
            break;
        }

        case events::OutboundEventType::OrderState: {
            const auto& p = event.order;
            std::string sym = "";
            auto sym_it = id_to_symbol.find(p.instrument_id);
            if (sym_it != id_to_symbol.end()) sym = sym_it->second;

            OrderRecord rec;
            rec.order_id = p.order_id;
            rec.instrument_id = p.instrument_id;
            rec.symbol = sym;
            rec.side = p.side;
            rec.price = p.price;
            rec.original_qty = p.original_qty;
            rec.remaining_qty = p.remaining_qty;
            rec.filled_qty = p.filled_qty;
            rec.status = p.status;
            rec.timestamp = p.timestamp;

            recordOrderInternal(rec);
            break;
        }
    }
}

bool ReadModel::getL2Book(InstrumentId id, L2BookState& out) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    auto it = l2_books.find(id);
    if (it != l2_books.end()) {
        out = it->second;
        return true;
    }
    // Return empty initialized state for registered instrument
    auto sym_it = id_to_symbol.find(id);
    if (sym_it != id_to_symbol.end()) {
        out = L2BookState{};
        out.instrument_id = id;
        out.symbol = sym_it->second;
        return true;
    }
    return false;
}

bool ReadModel::getL2BookBySymbol(const std::string& symbol, L2BookState& out) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    auto it = symbol_to_id.find(symbol);
    if (it == symbol_to_id.end()) return false;
    InstrumentId id = it->second;

    auto book_it = l2_books.find(id);
    if (book_it != l2_books.end()) {
        out = book_it->second;
        return true;
    }
    out = L2BookState{};
    out.instrument_id = id;
    out.symbol = symbol;
    return true;
}

bool ReadModel::getRecentTrades(InstrumentId id, size_t limit, std::vector<TradeRecord>& out) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    auto it = trade_histories.find(id);
    if (it != trade_histories.end()) {
        it->second.getRecent(limit, out);
        return true;
    }
    return false;
}

bool ReadModel::getRecentTradesBySymbol(const std::string& symbol, size_t limit, std::vector<TradeRecord>& out) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    auto it = symbol_to_id.find(symbol);
    if (it == symbol_to_id.end()) return false;
    InstrumentId id = it->second;

    auto th_it = trade_histories.find(id);
    if (th_it != trade_histories.end()) {
        th_it->second.getRecent(limit, out);
        return true;
    }
    return true; // empty trades for valid symbol
}

bool ReadModel::getOrder(OrderId id, OrderRecord& out) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    auto it = orders.find(id);
    if (it != orders.end()) {
        out = it->second;
        return true;
    }
    return false;
}

void ReadModel::getRegisteredSymbols(std::vector<std::pair<InstrumentId, std::string>>& out) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    out.reserve(id_to_symbol.size());
    for (const auto& kv : id_to_symbol) {
        out.push_back(kv);
    }
}

size_t ReadModel::getTradeCount(InstrumentId id) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    auto it = trade_histories.find(id);
    if (it != trade_histories.end()) {
        return it->second.size();
    }
    return 0;
}

size_t ReadModel::getOrderCount() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    return orders.size();
}
