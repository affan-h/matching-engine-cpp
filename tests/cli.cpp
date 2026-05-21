#include <iostream>
#include <sstream>
#include <string>
#include <iomanip>
#include "matching_engine.h"
#include "stats_tracker.h"

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

static void printHelp() {
    std::cout << R"(
Commands:
  register <SYMBOL>               Register a new instrument
  buy  <SYMBOL> <PRICE> <QTY>     Place GTC limit buy order
  sell <SYMBOL> <PRICE> <QTY>     Place GTC limit sell order
  ioc  buy|sell <SYMBOL> <PRICE> <QTY>   IOC limit order
  fok  buy|sell <SYMBOL> <PRICE> <QTY>   FOK limit order
  market buy|sell <SYMBOL> <QTY>  Market order
  cancel <SYMBOL> <ORDER_ID>      Cancel a resting order
  book <SYMBOL>                   Print order book
  stats                           Print market statistics
  help                            Show this message
  quit                            Exit
)";
}

static void printSnapshot(const L2Snapshot& snap) {
    std::cout << "\n  -- " << snap.symbol << " --\n";
    std::cout << "  ASKS:\n";
    for (int i = (int)snap.asks.size() - 1; i >= 0; --i) {
        auto& l = snap.asks[i];
        std::cout << "    " << std::setw(6) << l.price
                  << "  qty=" << std::setw(6) << l.volume
                  << "  orders=" << l.orderCount << "\n";
    }
    std::cout << "  ----spread=" << snap.spread() << "----\n";
    for (auto& l : snap.bids) {
        std::cout << "    " << std::setw(6) << l.price
                  << "  qty=" << std::setw(6) << l.volume
                  << "  orders=" << l.orderCount << "\n";
    }
    std::cout << "  mid=" << std::fixed << std::setprecision(1)
              << snap.midPrice() << "\n\n";
}

// ─────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────

int main() {
    MatchingEngine engine;
    StatsTracker   tracker;

    // Wire stats tracker
    engine.subscribeMarketData([&](const L2Snapshot& snap) {
        tracker.onSnapshot(snap);
    });
    engine.subscribeTradeData([&](InstrumentId id, const std::string& sym,
                                   Price px, Quantity qty) {
        tracker.recordTrade(id, sym, px, qty);
    });

    // Wire book display on every change
    engine.subscribeMarketData([&](const L2Snapshot& snap) {
        printSnapshot(snap);
    });

    std::cout << "===== Matching Engine CLI =====\n";
    std::cout << "Type 'help' for commands.\n\n";

    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        // ── quit ──────────────────────────────
        if (cmd == "quit" || cmd == "exit") {
            break;
        }

        // ── help ──────────────────────────────
        else if (cmd == "help") {
            printHelp();
        }

        // ── register ──────────────────────────
        else if (cmd == "register") {
            std::string sym;
            if (!(ss >> sym)) {
                std::cout << "Usage: register <SYMBOL>\n"; continue;
            }
            InstrumentId id = engine.registerInstrument(sym);
            std::cout << "Registered " << sym << " as instrument " << id << "\n";
        }

        // ── buy ───────────────────────────────
        else if (cmd == "buy") {
            std::string sym; int price, qty;
            if (!(ss >> sym >> price >> qty)) {
                std::cout << "Usage: buy <SYMBOL> <PRICE> <QTY>\n"; continue;
            }
            if (!engine.symbolExists(sym)) {
                std::cout << "Unknown symbol. Use 'register' first.\n"; continue;
            }
            InstrumentId id = engine.getInstrumentId(sym);
            OrderId oid = engine.addLimitOrder(id, Side::Buy, price, qty,
                                                TimeInForce::GTC);
            std::cout << "BUY order placed. OrderId=" << oid << "\n";
        }

        // ── sell ──────────────────────────────
        else if (cmd == "sell") {
            std::string sym; int price, qty;
            if (!(ss >> sym >> price >> qty)) {
                std::cout << "Usage: sell <SYMBOL> <PRICE> <QTY>\n"; continue;
            }
            if (!engine.symbolExists(sym)) {
                std::cout << "Unknown symbol. Use 'register' first.\n"; continue;
            }
            InstrumentId id = engine.getInstrumentId(sym);
            OrderId oid = engine.addLimitOrder(id, Side::Sell, price, qty,
                                                TimeInForce::GTC);
            std::cout << "SELL order placed. OrderId=" << oid << "\n";
        }

        // ── ioc ───────────────────────────────
        else if (cmd == "ioc") {
            std::string dir, sym; int price, qty;
            if (!(ss >> dir >> sym >> price >> qty)) {
                std::cout << "Usage: ioc buy|sell <SYMBOL> <PRICE> <QTY>\n"; continue;
            }
            if (!engine.symbolExists(sym)) {
                std::cout << "Unknown symbol.\n"; continue;
            }
            Side side = (dir == "buy") ? Side::Buy : Side::Sell;
            InstrumentId id = engine.getInstrumentId(sym);
            OrderId oid = engine.addLimitOrder(id, side, price, qty,
                                                TimeInForce::IOC);
            std::cout << "IOC order placed. OrderId=" << oid << "\n";
        }

        // ── fok ───────────────────────────────
        else if (cmd == "fok") {
            std::string dir, sym; int price, qty;
            if (!(ss >> dir >> sym >> price >> qty)) {
                std::cout << "Usage: fok buy|sell <SYMBOL> <PRICE> <QTY>\n"; continue;
            }
            if (!engine.symbolExists(sym)) {
                std::cout << "Unknown symbol.\n"; continue;
            }
            Side side = (dir == "buy") ? Side::Buy : Side::Sell;
            InstrumentId id = engine.getInstrumentId(sym);
            uint64_t before = engine.getTotalTrades();
            OrderId oid = engine.addLimitOrder(id, side, price, qty,
                                                TimeInForce::FOK);
            if (engine.getTotalTrades() == before)
                std::cout << "FOK rejected — insufficient liquidity.\n";
            else
                std::cout << "FOK filled. OrderId=" << oid << "\n";
        }

        // ── market ────────────────────────────
        else if (cmd == "market") {
            std::string dir, sym; int qty;
            if (!(ss >> dir >> sym >> qty)) {
                std::cout << "Usage: market buy|sell <SYMBOL> <QTY>\n"; continue;
            }
            if (!engine.symbolExists(sym)) {
                std::cout << "Unknown symbol.\n"; continue;
            }
            Side side = (dir == "buy") ? Side::Buy : Side::Sell;
            InstrumentId id = engine.getInstrumentId(sym);
            engine.addMarketOrder(id, side, qty);
            std::cout << "Market order executed.\n";
        }

        // ── cancel ────────────────────────────
        else if (cmd == "cancel") {
            std::string sym; OrderId oid;
            if (!(ss >> sym >> oid)) {
                std::cout << "Usage: cancel <SYMBOL> <ORDER_ID>\n"; continue;
            }
            if (!engine.symbolExists(sym)) {
                std::cout << "Unknown symbol.\n"; continue;
            }
            InstrumentId id = engine.getInstrumentId(sym);
            bool ok = engine.cancelOrder(id, oid);
            std::cout << (ok ? "Order cancelled.\n" : "Order not found.\n");
        }

        // ── book ──────────────────────────────
        else if (cmd == "book") {
            std::string sym;
            if (!(ss >> sym)) {
                std::cout << "Usage: book <SYMBOL>\n"; continue;
            }
            if (!engine.symbolExists(sym)) {
                std::cout << "Unknown symbol.\n"; continue;
            }
            engine.printOrderBook(engine.getInstrumentId(sym));
        }

        // ── stats ─────────────────────────────
        else if (cmd == "stats") {
            tracker.printAll();
        }

        else {
            std::cout << "Unknown command. Type 'help'.\n";
        }
    }

    std::cout << "\nGoodbye.\n";
    return 0;
}