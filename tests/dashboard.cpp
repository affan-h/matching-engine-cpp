#include <ncurses.h>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <ctime>
#include <chrono>
#include <climits>
#include <functional>
#include "matching_engine.h"
#include "stats_tracker.h"

// ─────────────────────────────────────────────
// Layout constants
// ─────────────────────────────────────────────
static const int LEFT_W  = 20;  // Widened to utilize space
static const int RIGHT_W = 35;  // Widened to utilize space
static const int STATS_H = 3;
static const int CMD_H   = 4;
static const int HDR_H   = 1;

// ─────────────────────────────────────────────
// Data types
// ─────────────────────────────────────────────
struct TradeEntry {
    std::string symbol;
    std::string side;
    Price       price;
    Quantity    qty;
    std::string time;
};

struct RestingOrder {
    OrderId     id;
    std::string side;
    Price       price;
    Quantity    origQty;
    std::string symbol;
};

// ─────────────────────────────────────────────
// Shared state
// ─────────────────────────────────────────────
struct DashboardState {
    mutable std::mutex mtx;

    std::vector<L2Snapshot>  snapshots;
    std::deque<TradeEntry>   trades;
    std::deque<RestingOrder> restingOrders;

    static const int MAX_TRADES  = 50;
    static const int MAX_RESTING = 200;

    void updateSnapshot(const L2Snapshot& snap) {
        std::lock_guard<std::mutex> lk(mtx);
        if (snap.instrument >= snapshots.size())
            snapshots.resize(snap.instrument + 1);
        snapshots[snap.instrument] = snap;
    }

    void addTrade(const std::string& sym, const std::string& side,
                  Price px, Quantity qty, const std::string& t) {
        std::lock_guard<std::mutex> lk(mtx);
        trades.push_front({sym, side, px, qty, t});
        if ((int)trades.size() > MAX_TRADES) trades.pop_back();
    }

    void addResting(const std::string& sym, const std::string& side,
                    Price px, Quantity qty, OrderId id) {
        std::lock_guard<std::mutex> lk(mtx);
        restingOrders.push_front({id, side, px, qty, sym});
        if ((int)restingOrders.size() > MAX_RESTING) restingOrders.pop_back();
    }

    void removeResting(OrderId id) {
        std::lock_guard<std::mutex> lk(mtx);
        restingOrders.erase(
            std::remove_if(restingOrders.begin(), restingOrders.end(),
                [id](const RestingOrder& r){ return r.id == id; }),
            restingOrders.end());
    }
};

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────
static std::string currentTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&t);
    char buf[9];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    return std::string(buf);
}

static void drawBox(WINDOW* w, const std::string& title) {
    box(w, 0, 0);
    if (!title.empty()) {
        wattron(w, A_BOLD);
        mvwprintw(w, 0, 2, " %s ", title.c_str());
        wattroff(w, A_BOLD);
    }
}

static void clearInner(WINDOW* w) {
    int rows, cols;
    getmaxyx(w, rows, cols);
    for (int r = 1; r < rows - 1; ++r) {
        wmove(w, r, 1);
        for (int c = 1; c < cols - 1; ++c) waddch(w, ' ');
    }
}

static void drawBar(WINDOW* w, int row, int col, int maxWidth, Quantity vol, Quantity maxVol, int colorPair) {
    int wRows, wCols;
    getmaxyx(w, wRows, wCols);
    if (maxVol <= 0 || maxWidth <= 0 || row <= 0 || row >= wRows - 1 || col <= 0 || col >= wCols - 1) return;

    int available = std::min(maxWidth, wCols - col - 2);
    if (available <= 0) return;

    int filled = (int)((double)vol / maxVol * available);
    filled = std::max(0, std::min(filled, available));

    wattron(w, COLOR_PAIR(colorPair) | A_DIM);
    for (int i = 0; i < filled; ++i) {
        if (col + i >= wCols - 1) break;
        // Use a solid block character for a cleaner look
        mvwaddch(w, row, col + i, '|'); 
    }
    wattroff(w, COLOR_PAIR(colorPair) | A_DIM);
}

// ─────────────────────────────────────────────
// Render: header
// ─────────────────────────────────────────────
static void renderHeader(WINDOW* w, uint64_t trades, const std::string& time) {
    wclear(w);
    wattron(w, A_BOLD | COLOR_PAIR(3));
    mvwprintw(w, 0, 0,
        " L2 MATCHING ENGINE  Trades:%-5llu  %s  "
        " [UP][DN] switch instrument   [,] [.] scroll resting   q=quit ",
        (unsigned long long)trades, time.c_str());
    wattroff(w, A_BOLD | COLOR_PAIR(3));
    wrefresh(w);
}

// ─────────────────────────────────────────────
// Render: instrument list
// ─────────────────────────────────────────────
static void renderLeft(WINDOW* w, const std::vector<std::string>& symbols, int selected) {
    clearInner(w);
    drawBox(w, "SYMBOLS");
    for (int i = 0; i < (int)symbols.size(); ++i) {
        if (i == selected) {
            wattron(w, A_REVERSE | A_BOLD);
            mvwprintw(w, 2 + i, 2, " %-14s ", symbols[i].c_str());
            wattroff(w, A_REVERSE | A_BOLD);
        } else {
            mvwprintw(w, 2 + i, 2, " %-14s ", symbols[i].c_str());
        }
    }
    wrefresh(w);
}

// ─────────────────────────────────────────────
// Render: center panel (Book + Resting)
// ─────────────────────────────────────────────
static void renderCenter(WINDOW* w, const DashboardState& state, MatchingEngine& engine,
                          int instrId, const std::string& symbol, int restingScroll) {
    clearInner(w);
    drawBox(w, "MARKET DEPTH: " + symbol);

    int totalRows = getmaxy(w);
    int cols      = getmaxx(w);

    // Hardcode resting orders space to show exactly 8 orders + header + divider
    int restingRows = 10; 
    int bookRows    = std::max(5, totalRows - restingRows);
    int divRow      = bookRows;

    // Wider bar width for better aesthetics
    int barWidth = 20;
    int barCol   = std::max(35, cols - barWidth - 4);

    L2Snapshot snap;
    bool hasSnap = false;
    {
        std::lock_guard<std::mutex> lk(state.mtx);
        if (instrId < (int)state.snapshots.size()) {
            snap    = state.snapshots[instrId];
            hasSnap = true;
        }
    }

    int row = 1;
    int bookBottom = divRow;

    // Book Column Headers
    wattron(w, A_DIM | A_UNDERLINE);
    mvwprintw(w, row++, 2, "  %-4s   %-8s   %-8s ", "TYPE", "PRICE", "VOLUME");
    wattroff(w, A_DIM | A_UNDERLINE);

    if (!hasSnap || (snap.bids.empty() && snap.asks.empty())) {
        mvwprintw(w, row + 1, 4, "Order book is empty.");
    } else {
        Quantity maxVol = 1;
        for (auto& l : snap.asks) maxVol = std::max(maxVol, l.volume);
        for (auto& l : snap.bids) maxVol = std::max(maxVol, l.volume);

        int askTotal = (int)snap.asks.size();
        int bidTotal = (int)snap.bids.size();

        int maxItems = bookBottom - row - 1; // Remaining rows minus spread line
        int askAlloc = maxItems / 2;
        int bidAlloc = maxItems - askAlloc;

        // Shift empty space from an empty side to the heavy side
        if (askTotal < askAlloc) {
            bidAlloc = std::min(bidTotal, maxItems - askTotal);
            askAlloc = askTotal;
        } else if (bidTotal < bidAlloc) {
            askAlloc = std::min(askTotal, maxItems - bidTotal);
            bidAlloc = bidTotal;
        }

        int aCount = std::min(askTotal, askAlloc);
        int bCount = std::min(bidTotal, bidAlloc);

        // ── Asks ──
        if (askTotal > 0) {
            wattron(w, COLOR_PAIR(1));
            // Print asks bottom-up so best ask is nearest the spread
            for (int i = aCount - 1; i >= 0 && row < bookBottom - 1; --i) {
                auto& l = snap.asks[i];
                mvwprintw(w, row, 2, "  ASK    %8d   %-8d", l.price, l.volume);
                drawBar(w, row, barCol, barWidth, l.volume, maxVol, 1);
                ++row;
            }
            wattroff(w, COLOR_PAIR(1));
            
            // Subtle hidden ask indicator
            if (askTotal > aCount) {
                wattron(w, A_DIM | COLOR_PAIR(1));
                mvwprintw(w, 2, cols - 15, "[+%d asks]", askTotal - aCount);
                wattroff(w, A_DIM | COLOR_PAIR(1));
            }
        }

        // ── Spread ──
        wattron(w, A_BOLD | COLOR_PAIR(3));
        std::string spreadLine;
        if (snap.spread() >= 0) {
            std::ostringstream ss;
            ss << " SPREAD: " << snap.spread() << "  |  MID: " << std::fixed << std::setprecision(1) << snap.midPrice() << " ";
            spreadLine = ss.str();
        } else {
            spreadLine = " --- SINGLE SIDED MARKET --- ";
        }
        int pad = std::max(2, (cols - 2 - (int)spreadLine.size()) / 2);
        if (row < bookBottom) mvwprintw(w, row++, pad, "%s", spreadLine.c_str());
        wattroff(w, A_BOLD | COLOR_PAIR(3));

        // ── Bids ──
        if (bidTotal > 0) {
            wattron(w, COLOR_PAIR(2));
            for (int i = 0; i < bCount && row < bookBottom; ++i) {
                auto& l = snap.bids[i];
                mvwprintw(w, row, 2, "  BID    %8d   %-8d", l.price, l.volume);
                drawBar(w, row, barCol, barWidth, l.volume, maxVol, 2);
                ++row;
            }
            wattroff(w, COLOR_PAIR(2));

            // Subtle hidden bid indicator
            if (bidTotal > bCount) {
                wattron(w, A_DIM | COLOR_PAIR(2));
                mvwprintw(w, bookBottom - 1, cols - 15, "[+%d bids]", bidTotal - bCount);
                wattroff(w, A_DIM | COLOR_PAIR(2));
            }
        }
    }

    // ── Divider & Resting ─────────────────────
    wattron(w, A_DIM);
    mvwhline(w, divRow, 1, ACS_HLINE, cols - 2);
    mvwprintw(w, divRow, 4, " RESTING ORDERS  (cancel %s <ID>) ", symbol.c_str());
    wattroff(w, A_DIM);

    std::vector<RestingOrder> local;
    {
        std::lock_guard<std::mutex> lk(state.mtx);
        for (auto& r : state.restingOrders)
            if (r.symbol == symbol) local.push_back(r);
    }

    std::vector<std::pair<RestingOrder, Order>> live;
    for (auto& r : local) {
        Order current;
        if (engine.getOrder(instrId, r.id, current))
            live.push_back({r, current});
    }

    int rrow = divRow + 1;
    int rBottom = totalRows - 1;

    if (live.empty()) {
        if (rrow < rBottom) mvwprintw(w, rrow + 1, 4, "No active resting orders.");
    } else {
        wattron(w, A_DIM | A_UNDERLINE);
        mvwprintw(w, rrow++, 2, "  %-7s %-6s  %-8s %-8s %-8s %-6s", "ORDER", "SIDE", "PRICE", "ORIG", "REM", "FILL%");
        wattroff(w, A_DIM | A_UNDERLINE);

        int total = (int)live.size();
        int scroll = std::min(restingScroll, std::max(0, total - (rBottom - rrow - 1)));

        for (int idx = scroll; idx < total; ++idx) {
            if (rrow >= rBottom - 1) {
                wattron(w, A_DIM);
                mvwprintw(w, rrow, 2, "  ... %d more orders below  , to scroll", total - idx);
                wattroff(w, A_DIM);
                break;
            }
            auto& [r, current] = live[idx];
            bool isBuy = (r.side == "buy");
            int fillPct = r.origQty > 0 ? (int)(100.0 * (r.origQty - current.quantity) / r.origQty) : 0;

            if (isBuy) wattron(w, COLOR_PAIR(2));
            else        wattron(w, COLOR_PAIR(1));

            mvwprintw(w, rrow, 2, "  #%-6llu %-6s  %-8d %-8d %-8d %3d%%%s",
                      (unsigned long long)r.id, isBuy ? "BUY" : "SELL",
                      r.price, r.origQty, current.quantity, fillPct, (current.quantity < r.origQty) ? " *" : "  ");
            ++rrow;

            if (isBuy) wattroff(w, COLOR_PAIR(2));
            else        wattroff(w, COLOR_PAIR(1));
        }
    }
    wrefresh(w);
}

// ─────────────────────────────────────────────
// Render: recent trades
// ─────────────────────────────────────────────
static void renderRight(WINDOW* w, const DashboardState& state, const std::string& symbol) {
    clearInner(w);
    drawBox(w, "TIME & SALES");

    std::vector<TradeEntry> local;
    {
        std::lock_guard<std::mutex> lk(state.mtx);
        for (auto& t : state.trades)
            if (t.symbol == symbol) local.push_back(t);
    }

    wattron(w, A_DIM | A_UNDERLINE);
    mvwprintw(w, 1, 2, " %-4s   %-6s   %-6s  %-8s", "SIDE", "QTY", "PRICE", "TIME");
    wattroff(w, A_DIM | A_UNDERLINE);

    int row = 2, maxRow = getmaxy(w) - 1;
    if (local.empty()) {
        mvwprintw(w, row + 1, 4, "No executions.");
    } else {
        for (auto& t : local) {
            if (row >= maxRow) break;
            bool isBuy = (t.side == "BUY");
            if (isBuy) wattron(w, COLOR_PAIR(2));
            else        wattron(w, COLOR_PAIR(1));
            mvwprintw(w, row++, 2, " %-4s   %-6d   %-6d  %s", t.side.c_str(), t.qty, t.price, t.time.c_str());
            if (isBuy) wattroff(w, COLOR_PAIR(2));
            else        wattroff(w, COLOR_PAIR(1));
        }
    }
    wrefresh(w);
}

// ─────────────────────────────────────────────
// Render: stats bar
// ─────────────────────────────────────────────
static void renderStats(WINDOW* w, const StatsTracker& tracker, InstrumentId id, const std::string& symbol) {
    clearInner(w);
    drawBox(w, "");
    const InstrumentStats* s = tracker.getStats(id);
    if (!s || s->totalVolume == 0) {
        mvwprintw(w, 1, 2, " %s : Awaiting market data...", symbol.c_str());
    } else {
        std::string lowAskStr = (s->lowAsk < INT_MAX) ? std::to_string(s->lowAsk) : "N/A";
        mvwprintw(w, 1, 2, " %-10s |  VWAP: %-8.2f |  Volume: %-10llu |  Cur Spread: %-4d |  Avg Spread: %-5.1f |  HiBid: %-6d |  LoAsk: %s",
            symbol.c_str(), s->vwap, (unsigned long long)s->totalVolume,
            s->lastSpread >= 0 ? s->lastSpread : 0, s->avgSpread, s->highBid, lowAskStr.c_str());
    }
    wrefresh(w);
}

// ─────────────────────────────────────────────
// Render: command panel
// ─────────────────────────────────────────────
static void renderCmd(WINDOW* w, const std::string& cmdBuf, const std::string& statusMsg) {
    wclear(w);
    drawBox(w, "TERMINAL");
    mvwprintw(w, 1, 2, "> %s", cmdBuf.c_str());
    if (!statusMsg.empty()) {
        wattron(w, A_DIM);
        mvwprintw(w, 2, 2, "  System: %.*s", getmaxx(w) - 12, statusMsg.c_str());
        wattroff(w, A_DIM);
    }
    wmove(w, 1, 4 + (int)cmdBuf.size());
    wrefresh(w);
}

// ─────────────────────────────────────────────
// Command processing
// ─────────────────────────────────────────────
static std::string processCommand(const std::string& line, MatchingEngine& engine, DashboardState& state) {
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;

    auto checkSym = [&](const std::string& sym) -> bool { return engine.symbolExists(sym); };
    auto resolve  = [&](const std::string& sym) -> InstrumentId { return engine.getInstrumentId(sym); };

    if (cmd == "?" || cmd == "help") {
        return "buy/sell <SYM> <PX> <QTY> | ioc/fok buy/sell <SYM> <PX> <QTY> | market buy/sell <SYM> <QTY> | cancel <SYM> <ID>";
    }

    if (cmd == "buy" || cmd == "sell") {
        std::string sym, extra; int price, qty;
        if (!(ss >> sym >> price >> qty)) return "Usage: " + cmd + " <SYMBOL> <PRICE> <QTY>";
        if (ss >> extra) return "Error: Too many arguments.";
        if (price <= 0 || qty <= 0) return "Error: Price and Quantity must be > 0.";
        if (!checkSym(sym)) return "Unknown symbol: " + sym;
        
        Side side = (cmd == "buy") ? Side::Buy : Side::Sell;
        OrderId oid = engine.addLimitOrder(resolve(sym), side, price, qty, TimeInForce::GTC);
        Order check;
        if (engine.getOrder(resolve(sym), oid, check))
            state.addResting(sym, cmd, price, qty, oid);
        return cmd + " placed. ID=" + std::to_string(oid);
    }

    if (cmd == "ioc" || cmd == "fok") {
        std::string dir, sym, extra; int price, qty;
        if (dir != "buy" && dir != "sell") return "Error: direction must be 'buy' or 'sell'";
        if (!(ss >> dir >> sym >> price >> qty)) return "Usage: " + cmd + " buy|sell <SYMBOL> <PRICE> <QTY>";
        if (ss >> extra) return "Error: Too many arguments.";
        if (price <= 0 || qty <= 0) return "Error: Price and Quantity must be > 0.";
        if (!checkSym(sym)) return "Unknown symbol: " + sym;
        
        Side side = (dir == "buy") ? Side::Buy : Side::Sell;
        TimeInForce tif = (cmd == "ioc") ? TimeInForce::IOC : TimeInForce::FOK;
        uint64_t before = engine.getTotalTrades();
        OrderId oid = engine.addLimitOrder(resolve(sym), side, price, qty, tif);
        
        if (cmd == "ioc") {
            return "IOC: " + std::to_string(engine.getTotalTrades() - before) + " trade(s). ID=" + std::to_string(oid);
        } else {
            return (engine.getTotalTrades() == before) ? "FOK rejected: insufficient liquidity." : "FOK filled. ID=" + std::to_string(oid);
        }
    }

    if (cmd == "market") {
        std::string dir, sym, extra; int qty;
        if (dir != "buy" && dir != "sell") return "Error: direction must be 'buy' or 'sell'";
        if (!(ss >> dir >> sym >> qty)) return "Usage: market buy|sell <SYMBOL> <QTY>";
        if (ss >> extra) return "Error: Too many arguments.";
        if (qty <= 0) return "Error: Quantity must be > 0.";
        if (!checkSym(sym)) return "Unknown symbol: " + sym;
        
        Side side = (dir == "buy") ? Side::Buy : Side::Sell;
        uint64_t before = engine.getTotalTrades();
        engine.addMarketOrder(resolve(sym), side, qty);
        return "Market: " + std::to_string(engine.getTotalTrades() - before) + " trade(s).";
    }

    if (cmd == "cancel") {
        std::string sym, extra; OrderId oid;
        if (!(ss >> sym >> oid)) return "Usage: cancel <SYMBOL> <ORDER_ID>";
        if (ss >> extra) return "Error: Too many arguments.";
        if (!checkSym(sym)) return "Unknown symbol: " + sym;
        
        InstrumentId iId = resolve(sym);
        Order preCancelOrder;
        bool orderExists = engine.getOrder(iId, oid, preCancelOrder);

        RestingOrder ro;
        bool foundRo = false;
        {
            std::lock_guard<std::mutex> lk(state.mtx);
            for (auto& resting : state.restingOrders) {
                if (resting.id == oid) { ro = resting; foundRo = true; break; }
            }
        }

        bool ok = engine.cancelOrder(iId, oid);
        if (ok) {
            state.removeResting(oid);
            engine.refreshSnapshot(iId);  // immediately push fresh L2 snapshot
            return "Cancelled ID=" + std::to_string(oid);
        }
        return "Order not found.";
    }

    return "Unknown command. Type ? for help.";
}

// ─────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────
int main() {
    MatchingEngine engine;
    StatsTracker   tracker;
    DashboardState state;

    std::vector<std::string> symbols = {"AAPL", "RELIANCE", "INFY", "TATASTEEL"};
    std::vector<InstrumentId> ids;
    for (auto& sym : symbols)
        ids.push_back(engine.registerInstrument(sym));

    engine.subscribeMarketData([&](const L2Snapshot& snap) {
        tracker.onSnapshot(snap);
        state.updateSnapshot(snap);
    });

    engine.subscribeTradeData([&](InstrumentId id, const std::string& sym, Price px, Quantity qty, Side aggressor) {
        tracker.recordTrade(id, sym, px, qty, aggressor);
        std::string side = (aggressor == Side::Buy) ? "BUY" : "SELL";
        state.addTrade(sym, side, px, qty, currentTime());
    });

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);
    start_color();
    use_default_colors();
    init_pair(1, COLOR_RED,   -1);
    init_pair(2, COLOR_GREEN, -1);
    init_pair(3, COLOR_BLACK, COLOR_CYAN);

    int totalRows, totalCols;
    getmaxyx(stdscr, totalRows, totalCols);

    if (totalRows < 24 || totalCols < LEFT_W + RIGHT_W + 40) {
        endwin();
        std::cerr << "Terminal too small. Need at least 100 cols, 24 rows.\n";
        return 1;
    }

    int centerW = totalCols - LEFT_W - RIGHT_W;
    int innerH  = totalRows - HDR_H - STATS_H - CMD_H;

    WINDOW* winHdr    = newwin(HDR_H,   totalCols, 0,   0);
    WINDOW* winLeft   = newwin(innerH,  LEFT_W,    HDR_H, 0);
    WINDOW* winCenter = newwin(innerH,  centerW,   HDR_H, LEFT_W);
    WINDOW* winRight  = newwin(innerH,  RIGHT_W,   HDR_H, LEFT_W + centerW);
    WINDOW* winStats  = newwin(STATS_H, totalCols, HDR_H + innerH, 0);
    WINDOW* winCmd    = newwin(CMD_H,   totalCols, HDR_H + innerH + STATS_H, 0);

    keypad(winCmd, TRUE);
    wtimeout(winCmd, 150);

    int  selected      = 0;
    int  restingScroll = 0;
    std::string cmdBuf;
    std::string statusMsg;

    while (true) {
        std::string now = currentTime();
        renderHeader(winHdr, engine.getTotalTrades(), now);
        renderLeft(winLeft, symbols, selected);
        renderCenter(winCenter, state, engine, ids[selected], symbols[selected], restingScroll);
        renderRight(winRight, state, symbols[selected]);
        renderStats(winStats, tracker, ids[selected], symbols[selected]);
        renderCmd(winCmd, cmdBuf, statusMsg);

        {
            std::lock_guard<std::mutex> lk(state.mtx);
            state.restingOrders.erase(
                std::remove_if(state.restingOrders.begin(), state.restingOrders.end(),
                    [&](const RestingOrder& r) {
                        if (!engine.symbolExists(r.symbol)) return false;
                        Order tmp;
                        return !engine.getOrder(engine.getInstrumentId(r.symbol), r.id, tmp);
                    }),
                state.restingOrders.end());
        }

        int ch = wgetch(winCmd);
        if (ch == ERR) continue;

        if (ch == 'q' && cmdBuf.empty()) break;

        if (ch == KEY_UP && cmdBuf.empty()) {
            selected = (selected - 1 + (int)symbols.size()) % (int)symbols.size();
            restingScroll = 0; statusMsg.clear(); continue;
        }
        if (ch == KEY_DOWN && cmdBuf.empty()) {
            selected = (selected + 1) % (int)symbols.size();
            restingScroll = 0; statusMsg.clear(); continue;
        }

        if (ch == ',' && cmdBuf.empty()) { restingScroll += 5; continue; }
        if (ch == '.' && cmdBuf.empty()) { restingScroll = std::max(0, restingScroll - 5); continue; }

        if (ch == KEY_BACKSPACE || ch == 127) {
            if (!cmdBuf.empty()) cmdBuf.pop_back();
            continue;
        }

        if (ch == '\n' || ch == '\r') {
            if (!cmdBuf.empty()) {
                statusMsg     = processCommand(cmdBuf, engine, state);
                cmdBuf.clear();
                restingScroll = 0;
            }
            continue;
        }

        if (isprint(ch)) cmdBuf += (char)ch;
    }

    delwin(winHdr); delwin(winLeft); delwin(winCenter);
    delwin(winRight); delwin(winStats); delwin(winCmd);
    endwin();

    tracker.printAll();
    return 0;
}