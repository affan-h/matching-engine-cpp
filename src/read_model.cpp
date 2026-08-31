#include "read_model.h"
#include <algorithm>

ReadModel::ReadModel(size_t max_orders_cap, size_t max_trades_cap)
    : max_orders(max_orders_cap == 0 ? 1 : max_orders_cap),
      max_trades_per_symbol(max_trades_cap == 0 ? 1 : max_trades_cap) {}

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
    OrderRecord updated = record;
    auto it = orders.find(record.order_id);
    if (it != orders.end()) {
        // Prevent state regression: ignore stale updates with older sequence
        if (record.sequence > 0 && it->second.sequence > record.sequence) {
            return;
        }

        // Prevent state regression: terminal status (Filled, Cancelled, Rejected) cannot regress
        if ((it->second.status == events::OrderStatus::Filled ||
             it->second.status == events::OrderStatus::Cancelled ||
             it->second.status == events::OrderStatus::Rejected) &&
            (record.status == events::OrderStatus::New ||
             record.status == events::OrderStatus::PartiallyFilled)) {
            return;
        }

        // Prevent duplicate counting on terminal transitions
        if (it->second.status == record.status &&
            (record.status == events::OrderStatus::Filled ||
             record.status == events::OrderStatus::Cancelled ||
             record.status == events::OrderStatus::Rejected)) {
            return;
        }

        // Preserve client_order_id across fills if not present in this event
        if (updated.client_order_id == 0 && it->second.client_order_id != 0) {
            updated.client_order_id = it->second.client_order_id;
        }

        // Preserve original_qty across subsequent partial fills
        if (it->second.original_qty > 0) {
            updated.original_qty = it->second.original_qty;
        }

        // Compute exact cumulative filled_qty: original_qty - remaining_qty
        if (updated.original_qty >= updated.remaining_qty) {
            updated.filled_qty = updated.original_qty - updated.remaining_qty;
        }
    } else {
        // Enforce bounded capacity via FIFO eviction
        while (orders.size() >= max_orders && !order_eviction_queue.empty()) {
            OrderId oldest = order_eviction_queue.front();
            order_eviction_queue.pop_front();
            auto old_it = orders.find(oldest);
            if (old_it != orders.end()) {
                if (old_it->second.client_order_id != 0) {
                    auto cl_it = client_to_order_id.find(old_it->second.client_order_id);
                    if (cl_it != client_to_order_id.end() && cl_it->second == old_it->first) {
                        client_to_order_id.erase(cl_it);
                    }
                }
                orders.erase(old_it);
            }
        }
        order_eviction_queue.push_back(updated.order_id);
    }

    if (updated.client_order_id != 0) {
        client_to_order_id[updated.client_order_id] = updated.order_id;
    }

    // Update aggregate status metrics
    switch (updated.status) {
        case events::OrderStatus::New:
            if (it == orders.end()) {
                metrics.total_orders_accepted++;
            }
            break;
        case events::OrderStatus::Filled:
            metrics.total_orders_filled++;
            break;
        case events::OrderStatus::Cancelled:
            metrics.total_orders_cancelled++;
            break;
        case events::OrderStatus::Rejected:
            metrics.total_orders_rejected++;
            break;
        default:
            break;
    }

    orders[updated.order_id] = updated;
}

void ReadModel::applyEvent(const events::OutboundEvent& event) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);

    switch (event.type) {
        case events::OutboundEventType::Trade: {
            const auto& p = event.trade;
            if (p.sequence > 0) {
                metrics.last_sequence = std::max(metrics.last_sequence, p.sequence);
            }
            metrics.total_trades++;
            metrics.total_volume += p.quantity;

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
            tr.sequence = p.sequence;

            auto it = trade_histories.find(p.instrument_id);
            if (it == trade_histories.end()) {
                it = trade_histories.emplace(p.instrument_id, BoundedTradeHistory(max_trades_per_symbol)).first;
            }
            it->second.add(tr);
            break;
        }

        case events::OutboundEventType::L2Update: {
            const auto& p = event.l2;
            if (p.sequence > 0) {
                metrics.last_sequence = std::max(metrics.last_sequence, p.sequence);
            }

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
            if (p.sequence > 0) {
                metrics.last_sequence = std::max(metrics.last_sequence, p.sequence);
            }

            std::string sym = "";
            auto sym_it = id_to_symbol.find(p.instrument_id);
            if (sym_it != id_to_symbol.end()) sym = sym_it->second;

            OrderRecord rec;
            rec.order_id = p.order_id;
            rec.client_order_id = p.client_order_id;
            rec.instrument_id = p.instrument_id;
            rec.symbol = sym;
            rec.side = p.side;
            rec.price = p.price;
            rec.original_qty = p.original_qty;
            rec.remaining_qty = p.remaining_qty;
            rec.filled_qty = p.filled_qty;
            rec.status = p.status;
            rec.reject_code = p.reject_code;
            rec.timestamp = p.timestamp;
            rec.sequence = p.sequence;

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

bool ReadModel::getOrderByClientId(uint64_t client_order_id, OrderRecord& out) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    auto it = client_to_order_id.find(client_order_id);
    if (it != client_to_order_id.end()) {
        auto ord_it = orders.find(it->second);
        if (ord_it != orders.end()) {
            out = ord_it->second;
            return true;
        }
    }
    return false;
}

bool ReadModel::getMetrics(EngineMetrics& out) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    out = metrics;
    out.tracked_orders_count = orders.size();
    out.registered_symbols_count = id_to_symbol.size();
    return true;
}

uint64_t ReadModel::getLastSequence() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    return metrics.last_sequence;
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
